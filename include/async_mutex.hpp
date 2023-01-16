#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <atomic>
#include <functional>

using namespace std::chrono_literals;

namespace avast::asio {

class async_mutex_lock;
class async_mutex;

namespace detail {

struct locked_waiter {
    locked_waiter(locked_waiter *next_waiter)
        : next(next_waiter)
    {}
    virtual ~locked_waiter() = default;

    virtual void completion() = 0;

    locked_waiter *next = nullptr;
};

template<typename Token>
struct async_locked_waiter : public locked_waiter
{
    async_locked_waiter([[maybe_unused]] async_mutex *mutex, locked_waiter *next_waiter, Token &&token)
        : locked_waiter(next_waiter)
        , m_token(std::move(token))
    {}

    void completion() override {
        m_token();
    }

private:
    Token m_token;
};

template<typename Token>
struct scoped_async_locked_waiter : public locked_waiter
{
    scoped_async_locked_waiter(async_mutex *mutex, locked_waiter *next_waiter, Token &&token)
        : locked_waiter(next_waiter)
        , m_mutex(mutex)
        , m_token(std::move(token))
    {}

    void completion() override;

private:
    async_mutex *m_mutex;
    Token m_token;
};

template<typename Waiter>
class async_lock_initiator_base {
public:
    using waiter_type = Waiter;

    explicit async_lock_initiator_base(async_mutex *mutex): m_mutex(mutex) {}

    template<typename Handler>
    void operator()(Handler &&handler);

protected:
    async_mutex *m_mutex;
};

template<typename Token>
using initiate_async_lock = async_lock_initiator_base<async_locked_waiter<Token>>;
template<typename Token>
using initiate_scoped_async_lock = async_lock_initiator_base<scoped_async_locked_waiter<Token>>;

} // namespace detail

class async_mutex
{
public:
    async_mutex() noexcept = default;
    ~async_mutex()
    {
        [[maybe_unused]] const auto state = m_state.load(std::memory_order_relaxed);
        assert(state == not_locked || state == locked_no_waiters);
        assert(m_waiters == nullptr);
    }

    bool try_lock() noexcept
    {
        auto old_state = not_locked;
        return m_state.compare_exchange_strong(
            old_state, locked_no_waiters,
            std::memory_order_acquire, std::memory_order_relaxed);
    }

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) LockToken>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE_PREFIX(LockToken, void())
    lock_async(BOOST_ASIO_MOVE_ARG(LockToken) token)
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE_SUFFIX((
        async_initiate<LockToken, void()>(declval<detail::initiate_async_lock>(), token)))
    {
        using Handler = typename boost::asio::async_result<std::decay_t<LockToken>, void()>::handler_type;
        return boost::asio::async_initiate<LockToken, void()>(
            detail::initiate_async_lock<Handler>(this), token);
    }

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void(async_mutex_lock)) LockToken>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE_PREFIX(LockToken, void(async_mutex_lock))
    scoped_lock_async(BOOST_ASIO_MOVE_ARG(LockToken) token)
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE_SUFFIX((
        async_initiate<LockToken,
            void(async_mutex_lock)>(declval<detail::initiate_scoped_async_lock>(), token)))
    {
        using Handler = typename boost::asio::async_result<std::decay_t<LockToken>, void(async_mutex_lock)>::handler_type;
        return boost::asio::async_initiate<LockToken, void(async_mutex_lock)>(
            detail::initiate_scoped_async_lock<Handler>(this), token);
    }

    void unlock()
    {
        assert(m_state.load(std::memory_order_relaxed) != not_locked);

        auto *waiters_head = m_waiters;
        if (waiters_head == nullptr) {
            auto old_state = locked_no_waiters;
            const bool released_lock = m_state.compare_exchange_strong(
                old_state, not_locked, std::memory_order_release, std::memory_order_relaxed);
            if (released_lock) {
                return;
            }

            // At least one new waiter. Acquire the list of new waiters atomically
            old_state = m_state.exchange(locked_no_waiters, std::memory_order_acquire);

            assert(old_state != locked_no_waiters && old_state != not_locked);

            // Transfer the list to m_waiters, reversing the list in the process
            // so that the head of the list is the first waiter to be resumed
            auto *next = reinterpret_cast<detail::locked_waiter *>(old_state);
            do {
                auto *temp = next->next;
                next->next = waiters_head;
                waiters_head = next;
                next = temp;
            } while (next != nullptr);

        }

        assert(waiters_head != nullptr);

        m_waiters = waiters_head->next;

        // Complete the async operation.
        waiters_head->completion();
        delete waiters_head;
    }

private:
    template<typename Waiter>
    friend class detail::async_lock_initiator_base;

    static constexpr std::uintptr_t not_locked = 1;
    static constexpr std::uintptr_t locked_no_waiters = 0;
    std::atomic<std::uintptr_t> m_state = {not_locked};
    detail::locked_waiter *m_waiters = nullptr;
};

class async_mutex_lock
{
public:
    explicit async_mutex_lock(async_mutex &mutex, std::adopt_lock_t) noexcept
        : m_mutex(&mutex)
    {}

    async_mutex_lock(async_mutex_lock &&other) noexcept
        : m_mutex(other.m_mutex)
    {
        other.m_mutex = nullptr;
    }

    async_mutex_lock(const async_mutex_lock &) = delete;
    async_mutex_lock &operator=(const async_mutex_lock &) = delete;

    ~async_mutex_lock()
    {
        if (m_mutex) {
            m_mutex->unlock();
        }
    }

private:
    async_mutex *m_mutex;
};

namespace detail {

template<typename Token>
void scoped_async_locked_waiter<Token>::completion() {
    m_token(async_mutex_lock{*m_mutex, std::adopt_lock});
}

template<typename Waiter>
template<typename Handler>
void async_lock_initiator_base<Waiter>::operator()(Handler &&handler)
{
    auto old_state = m_mutex->m_state.load(std::memory_order_acquire);
    while (true) {
        if (old_state == async_mutex::not_locked) {
            if (m_mutex->m_state.compare_exchange_weak(
                old_state, async_mutex::locked_no_waiters,
                std::memory_order_acquire, std::memory_order_relaxed))
            {
                // Lock acquired, resume the awaiter stright away
                Waiter(m_mutex, nullptr, std::move(handler)).completion();
                return;
            }
        } else {
            auto *waiter = new Waiter(m_mutex, reinterpret_cast<locked_waiter *>(old_state), std::move(handler));
            if (m_mutex->m_state.compare_exchange_weak(
                        old_state, reinterpret_cast<std::uintptr_t>(waiter),
                        std::memory_order_release, std::memory_order_relaxed)) {
                return;
            }
        }
    }
}

} // namespace detail

} // namespace avast::asio



