#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/detached.hpp>

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

    boost::asio::io_context &ioc() { return m_ioc; }

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
    boost::asio::io_context m_ioc;
    std::thread m_thread;
    std::stop_source m_stop;
};

enum class EventType { Locking, Locked, Unlocked };

struct Event {
    std::size_t idx;
    EventType event;

    Event(std::size_t idx, EventType event): idx(idx), event(event) {}
    bool operator==(const Event &other) const = default;
};

using EventLog = std::vector<Event>;

TEST_CASE("try_lock") {
    avast::asio::async_mutex mutex;
    REQUIRE(mutex.try_lock());
    REQUIRE_FALSE(mutex.try_lock());
    mutex.unlock();
    REQUIRE(mutex.try_lock());
    mutex.unlock();
}

TEST_CASE("async_mutex_lock") {
    avast::asio::async_mutex mutex;
    REQUIRE(mutex.try_lock()); // lock is acquired
    {
        // async_mutex_lock holds the mutex
        avast::asio::async_mutex_lock lock(mutex, std::adopt_lock);
        // the mutex remains locked
        REQUIRE_FALSE(mutex.try_lock());

    }                          // the lock is destroyed and mutex should be unlocked
    REQUIRE(mutex.try_lock()); // lock is acquired
    mutex.unlock();
}

TEST_CASE("async_scoped_lock (simple)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](int idx) -> boost::asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        const auto lock = co_await mutex.async_scoped_lock(boost::asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        boost::asio::steady_timer timer{thread.ioc()};
        timer.expires_after(200ms);
        co_await timer.async_wait(boost::asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
    };

    boost::asio::co_spawn(thread.ioc(), testFunc(1), boost::asio::detached);
    boost::asio::co_spawn(thread.ioc(), testFunc(2), boost::asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(5s));

    REQUIRE(eventLog.size() == 6);
    REQUIRE(eventLog[0] == Event{1, EventType::Locking});
    REQUIRE(eventLog[1] == Event{1, EventType::Locked});
    REQUIRE(eventLog[2] == Event{2, EventType::Locking});
    REQUIRE(eventLog[3] == Event{1, EventType::Unlocked});
    REQUIRE(eventLog[4] == Event{2, EventType::Locked});
    REQUIRE(eventLog[5] == Event{2, EventType::Unlocked});
}

TEST_CASE("async_lock (simple)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> boost::asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(boost::asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        boost::asio::steady_timer timer{thread.ioc()};
        timer.expires_after(200ms);
        co_await timer.async_wait(boost::asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    boost::asio::co_spawn(thread.ioc(), testFunc(1), boost::asio::detached);
    boost::asio::co_spawn(thread.ioc(), testFunc(2), boost::asio::detached);
    thread.start();
    REQUIRE(thread.waitForFinished(5s));

    REQUIRE(eventLog.size() == 6);
    REQUIRE(eventLog[0] == Event{1, EventType::Locking});
    REQUIRE(eventLog[1] == Event{1, EventType::Locked});
    REQUIRE(eventLog[2] == Event{2, EventType::Locking});
    REQUIRE(eventLog[3] == Event{1, EventType::Unlocked});
    REQUIRE(eventLog[4] == Event{2, EventType::Locked});
    REQUIRE(eventLog[5] == Event{2, EventType::Unlocked});
}

TEST_CASE("async_lock (multiple coroutines)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> boost::asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(boost::asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        boost::asio::steady_timer timer{thread.ioc()};
        timer.expires_after(100ms);
        if (idx == 0) {
            co_await timer.async_wait(boost::asio::use_awaitable);
        }

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    for (std::size_t i = 0; i < 10; ++i) {
        boost::asio::co_spawn(thread.ioc(), testFunc(i), boost::asio::detached);
    }
    thread.start();
    REQUIRE(thread.waitForFinished(10s));

    REQUIRE(eventLog.size() == 30);
    REQUIRE(eventLog[0] == Event{0, EventType::Locking});
    REQUIRE(eventLog[1] == Event{0, EventType::Locked});
    for (std::size_t eventIdx = 2, testIdx = 1; eventIdx < 11; ++eventIdx, ++testIdx) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locking});
    }
    REQUIRE(eventLog[11] == Event{0, EventType::Unlocked});
    for (std::size_t eventIdx = 12, testIdx = 1; eventIdx < eventLog.size(); eventIdx += 2, testIdx += 1) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locked});
        REQUIRE(eventLog[eventIdx + 1] == Event{testIdx, EventType::Unlocked});
    }
}

TEST_CASE("async_lock (shallow stack)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> boost::asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(boost::asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        boost::asio::steady_timer timer{thread.ioc()};
        timer.expires_after(1ms);
        co_await timer.async_wait(boost::asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    size_t n = 50000;
    for (std::size_t i = 0; i < n; ++i) {
        boost::asio::co_spawn(thread.ioc(), testFunc(i), boost::asio::detached);
    }
    thread.start();
    REQUIRE(thread.waitForFinished(1000s));

    REQUIRE(eventLog.size() == 3 * n);
    REQUIRE(eventLog[0] == Event{0, EventType::Locking});
    REQUIRE(eventLog[1] == Event{0, EventType::Locked});
    for (std::size_t eventIdx = 2, testIdx = 1; eventIdx < n + 1; ++eventIdx, ++testIdx) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locking});
    }
    REQUIRE(eventLog[n + 1] == Event{0, EventType::Unlocked});
    for (std::size_t eventIdx = n + 2, testIdx = 1; eventIdx < eventLog.size(); eventIdx += 2, testIdx += 1) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locked});
        REQUIRE(eventLog[eventIdx + 1] == Event{testIdx, EventType::Unlocked});
    }
}

TEST_CASE("async_lock (deep stack)") {
    TestThread thread;
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&thread, &eventLog, &mutex](std::size_t idx) -> boost::asio::awaitable<void> {
        eventLog.emplace_back(idx, EventType::Locking);
        co_await mutex.async_lock(boost::asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        if (idx == 0) {
            boost::asio::steady_timer timer{thread.ioc()};
            timer.expires_after(1ms);
            co_await timer.async_wait(boost::asio::use_awaitable);
        }

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock(co_await boost::asio::this_coro::executor);
    };

    size_t n = 50000;
    for (std::size_t i = 0; i < n; ++i) {
        boost::asio::co_spawn(thread.ioc(), testFunc(i), boost::asio::detached);
    }
    thread.start();
    REQUIRE(thread.waitForFinished(1000s));

    REQUIRE(eventLog.size() == 3 * n);
    REQUIRE(eventLog[0] == Event{0, EventType::Locking});
    REQUIRE(eventLog[1] == Event{0, EventType::Locked});
    for (std::size_t eventIdx = 2, testIdx = 1; eventIdx < n + 1; ++eventIdx, ++testIdx) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locking});
    }
    REQUIRE(eventLog[n + 1] == Event{0, EventType::Unlocked});
    for (std::size_t eventIdx = n + 2, testIdx = 1; eventIdx < eventLog.size(); eventIdx += 2, testIdx += 1) {
        REQUIRE(eventLog[eventIdx] == Event{testIdx, EventType::Locked});
        REQUIRE(eventLog[eventIdx + 1] == Event{testIdx, EventType::Unlocked});
    }
}

TEST_CASE("async_lock (multithreaded)") {
    EventLog eventLog;
    avast::asio::async_mutex mutex;

    const auto testFunc = [&eventLog, &mutex](boost::asio::io_context &ioc,
                                              std::size_t idx) -> boost::asio::awaitable<void> {
        co_await mutex.async_lock(boost::asio::use_awaitable);
        eventLog.emplace_back(idx, EventType::Locked);

        boost::asio::steady_timer timer{ioc};
        timer.expires_after(100ms);
        co_await timer.async_wait(boost::asio::use_awaitable);

        eventLog.emplace_back(idx, EventType::Unlocked);
        mutex.unlock();
    };

    TestThread threads[10];
    for (std::size_t i = 0; i < 10; ++i) {
        boost::asio::co_spawn(threads[i].ioc(), testFunc(threads[i].ioc(), i), boost::asio::detached);
        threads[i].start();
    }

    for (auto &thread: threads) {
        REQUIRE(thread.waitForFinished(5s));
    }
}
