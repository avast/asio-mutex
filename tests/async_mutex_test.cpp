#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/detached.hpp>

#include <boost/asio/use_awaitable.hpp>
#include <coroutine>
#include <chrono>
#include <thread>
#include <stop_token>

#include "async_mutex.hpp"

#include <catch2/catch.hpp>

using namespace std::chrono_literals;

class TestThread {
public:
    explicit TestThread() = default;
    TestThread(TestThread &&) noexcept = delete;

    asio::io_context &ioc() { return m_ioc; }

    void start() {
        m_thread = std::thread([this]() {
            m_ioc.run();
            m_stop.request_stop();
        });
    }

    bool waitForFinished(std::chrono::milliseconds timeout) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        auto token = m_stop.get_token();
        while (std::chrono::steady_clock::now() < end) {
            if (token.stop_requested()) {
                m_thread.join();
                return true;
            }
        }

        m_ioc.stop();
        m_thread.join();
        return false;
    }

private:
    asio::io_context m_ioc;
    std::thread m_thread;
    std::stop_source m_stop;
};

enum class EventType { Locking, Locked, Unlocked };

std::ostream &operator<<(std::ostream &os, EventType v) {
    switch (v) {
        case EventType::Locking:
            os << "Locking";
            break;
        case EventType::Locked:
            os << "Locked";
            break;
        case EventType::Unlocked:
            os << "Unlocked";
            break;
        default:
            os << "?";
            break;
    }
    return os;
}

struct Event {
    std::size_t idx;
    EventType event;

    Event(std::size_t idx, EventType event): idx(idx), event(event) {}
    bool operator==(const Event &other) const = default;
};

std::ostream &operator<<(std::ostream &os, const Event &v) {
    os << "Event{" << v.idx << ", " << v.event << "}";
    return os;
}

using EventLog = std::vector<Event>;

TEST_CASE("try_lock") {
    avast::asio::async_mutex mutex;
    const auto testFunc = [&mutex]() -> asio::awaitable<void> {
        REQUIRE(mutex.try_lock());
        REQUIRE_FALSE(mutex.try_lock());
        mutex.unlock();
        REQUIRE(mutex.try_lock());
        co_return;
    };
    asio::io_context ctx;
    asio::co_spawn(ctx, testFunc, asio::detached);
    ctx.run();
}

TEST_CASE("async_mutex_lock") {
    avast::asio::async_mutex mutex;
    const auto testFunc = [&mutex]() -> asio::awaitable<void> {
        REQUIRE(mutex.try_lock()); // lock is acquired
        {
            // async_mutex_lock holds the mutex
            avast::asio::async_mutex_lock lock(mutex, std::adopt_lock);
            // the mutex remains locked
            REQUIRE_FALSE(mutex.try_lock());
        }                          // the lock is destroyed and mutex unlock should be scheduled
        REQUIRE(mutex.try_lock()); // lock is acquired
        mutex.unlock();
        co_return;
    };
    asio::io_context ctx;
    asio::co_spawn(ctx, testFunc, asio::detached);
    ctx.run();
}

TEST_CASE("async_scoped_lock (simple)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](int idx) -> asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        const auto lock = co_await mutex.async_scoped_lock(asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        asio::steady_timer timer{thread.ioc()};
        timer.expires_after(200ms);
        co_await timer.async_wait(asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
    };

    asio::co_spawn(thread.ioc(), testFunc(1), asio::detached);
    asio::co_spawn(thread.ioc(), testFunc(2), asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(5s));

    REQUIRE(eventLog.size() == 6);
    REQUIRE(eventLog[0] == Event{1, EventType::Locking});
    REQUIRE(eventLog[1] == Event{2, EventType::Locking});
    REQUIRE(eventLog[2] == Event{1, EventType::Locked});
    REQUIRE(eventLog[3] == Event{1, EventType::Unlocked});
    REQUIRE(eventLog[4] == Event{2, EventType::Locked});
    REQUIRE(eventLog[5] == Event{2, EventType::Unlocked});
}

TEST_CASE("async_lock (simple)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        asio::steady_timer timer{thread.ioc()};
        timer.expires_after(200ms);
        co_await timer.async_wait(asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    asio::co_spawn(thread.ioc(), testFunc(1), asio::detached);
    asio::co_spawn(thread.ioc(), testFunc(2), asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(5s));

    REQUIRE(eventLog.size() == 6);
    REQUIRE(eventLog[0] == Event{1, EventType::Locking});
    REQUIRE(eventLog[1] == Event{2, EventType::Locking});
    REQUIRE(eventLog[2] == Event{1, EventType::Locked});
    REQUIRE(eventLog[3] == Event{1, EventType::Unlocked});
    REQUIRE(eventLog[4] == Event{2, EventType::Locked});
    REQUIRE(eventLog[5] == Event{2, EventType::Unlocked});
}

TEST_CASE("async_lock (multiple coroutines)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        asio::steady_timer timer{thread.ioc()};
        timer.expires_after(100ms);
        if (idx == 0) {
            co_await timer.async_wait(asio::use_awaitable);
        }

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    for (std::size_t i = 0; i < 10; ++i) {
        asio::co_spawn(thread.ioc(), testFunc(i), asio::detached);
    }
    thread.start();
    REQUIRE(thread.waitForFinished(10s));

    REQUIRE(eventLog.size() == 30);
    for (std::size_t eventIdx = 0, testIdx = 0; eventIdx < 10; ++eventIdx, ++testIdx) {
        CHECK(eventLog[eventIdx] == Event{testIdx, EventType::Locking});
    }
    for (std::size_t eventIdx = 10, testIdx = 0; eventIdx < eventLog.size(); eventIdx += 2, testIdx += 1) {
        CHECK(eventLog[eventIdx] == Event{testIdx, EventType::Locked});
        CHECK(eventLog[eventIdx + 1] == Event{testIdx, EventType::Unlocked});
    }
}

TEST_CASE("async_lock (shallow stack)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        asio::steady_timer timer{thread.ioc()};
        timer.expires_after(1ms);
        co_await timer.async_wait(asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    size_t n = 500;
    for (std::size_t i = 0; i < n; ++i) {
        asio::co_spawn(thread.ioc(), testFunc(i), asio::detached);
    }
    thread.start();
    REQUIRE(thread.waitForFinished(1000s));

    REQUIRE(eventLog.size() == 3 * n);
    for (std::size_t eventIdx = 0, testIdx = 0; eventIdx < n; ++eventIdx, ++testIdx) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locking});
    }
    for (std::size_t eventIdx = n, testIdx = 0; eventIdx < eventLog.size(); eventIdx += 2, testIdx += 1) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locked});
        REQUIRE(eventLog[eventIdx + 1] == Event{testIdx, EventType::Unlocked});
    }
}

TEST_CASE("async_lock (deep stack)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        if (idx == 0) {
            asio::steady_timer timer{thread.ioc()};
            timer.expires_after(1ms);
            co_await timer.async_wait(asio::use_awaitable);
        }

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    size_t n = 50000;
    for (std::size_t i = 0; i < n; ++i) {
        asio::co_spawn(thread.ioc(), testFunc(i), asio::detached);
    }
    thread.start();
    REQUIRE(thread.waitForFinished(1000s));

    REQUIRE(eventLog.size() == 3 * n);
    for (std::size_t eventIdx = 0, testIdx = 0; eventIdx < n; ++eventIdx, ++testIdx) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locking});
    }
    REQUIRE(eventLog[n + 1] == Event{0, EventType::Unlocked});
    for (std::size_t eventIdx = n, testIdx = 0; eventIdx < eventLog.size(); eventIdx += 2, testIdx += 1) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locked});
        REQUIRE(eventLog[eventIdx + 1] == Event{testIdx, EventType::Unlocked});
    }
}

TEST_CASE("async_lock (multithreaded)") {
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&eventLog, &mutex](asio::io_context &ioc, std::size_t idx) -> asio::awaitable<void> {
        co_await mutex.async_lock(asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        asio::steady_timer timer{ioc};
        timer.expires_after(100ms);
        co_await timer.async_wait(asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    TestThread threads[10];
    for (std::size_t i = 0; i < 10; ++i) {
        asio::co_spawn(threads[i].ioc(), testFunc(threads[i].ioc(), i), asio::detached);
        threads[i].start();
    }

    for (auto &thread: threads) {
        REQUIRE(thread.waitForFinished(5s));
    }
}

TEST_CASE("async_scoped_lock") {
    avast::asio::async_mutex_lock lock;
    REQUIRE_FALSE(lock.owns_lock());
}

TEST_CASE("async_scoped_lock move init") {
    const auto testFunc = []() -> asio::awaitable<void> {
        avast::asio::async_mutex mutex;
        avast::asio::async_mutex_lock lock(co_await mutex.async_scoped_lock(asio::use_awaitable));
        REQUIRE(lock.owns_lock());
    };

    TestThread thread;
    asio::co_spawn(thread.ioc(), testFunc(), asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(1s));
}

TEST_CASE("async_scoped_lock move assignment") {
    const auto testFunc = []() -> asio::awaitable<void> {
        avast::asio::async_mutex_lock lock;
        REQUIRE_FALSE(lock.owns_lock());

        avast::asio::async_mutex mutex1;
        lock = co_await mutex1.async_scoped_lock(asio::use_awaitable);
        REQUIRE(lock.owns_lock());
        REQUIRE(lock.mutex() == &mutex1);
        REQUIRE_FALSE(mutex1.try_lock()); // locked by lock

        avast::asio::async_mutex mutex2;
        lock = co_await mutex2.async_scoped_lock(asio::use_awaitable);
        REQUIRE(lock.owns_lock());
        REQUIRE(lock.mutex() == &mutex2);
        REQUIRE(mutex1.try_lock());       // mutex1 should've been released
        mutex1.unlock();                  // make sure to unlock mutex1 now
        REQUIRE_FALSE(mutex2.try_lock()); // mutex2 is locked by the lock now
    };

    TestThread thread;
    asio::co_spawn(thread.ioc(), testFunc(), asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(1s));
}

TEST_CASE("async_scoped_lock swap") {
    const auto testFunc = []() -> asio::awaitable<void> {
        avast::asio::async_mutex mutex1;
        avast::asio::async_mutex mutex2;
        auto lock1 = co_await mutex1.async_scoped_lock(asio::use_awaitable);
        REQUIRE(lock1.mutex() == &mutex1);
        auto lock2 = co_await mutex2.async_scoped_lock(asio::use_awaitable);
        REQUIRE(lock2.mutex() == &mutex2);

        lock1.swap(lock2);

        // The mutexes owned by the locks should be swapped now
        REQUIRE(lock1.mutex() == &mutex2);
        REQUIRE(lock2.mutex() == &mutex1);

        // And should remained locked
        REQUIRE_FALSE(mutex1.try_lock());
        REQUIRE_FALSE(mutex2.try_lock());
    };

    TestThread thread;
    asio::co_spawn(thread.ioc(), testFunc(), asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(1s));
}
