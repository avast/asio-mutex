#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <atomic>
#include <functional>
#include <iostream>

namespace avast::asio {

class async_mutex_lock;
class async_mutex;

/** \internal **/
namespace detail {

/**
 * \brief Represents a suspended coroutine that is awaiting lock acquisition.
 **/
struct locked_waiter {
    /**
     * \brief Constructs a new locked_waiter.
     * \param next_waiter Pointer to the waiter to prepend this locked_waiter to.
     **/
    explicit locked_waiter(locked_waiter *next_waiter): next(next_waiter) {}
    locked_waiter(locked_waiter &&) = delete;
    locked_waiter(const locked_waiter &) = delete;
    locked_waiter &operator=(locked_waiter &&) = delete;
    locked_waiter &operator=(const locked_waiter &) = delete;
    /**
     * \brief Destructor.
     **/
    virtual ~locked_waiter() = default;

    /**
     * \brief Completes the pending asynchronous operation.
     *
     * Resumes the currently suspended coroutine with the acquired lock.
     **/
    virtual void completion() = 0;

    /**
     * The waiters are held in a linked list. This is a pointer to the next member of the list.
     **/
    locked_waiter *next = nullptr;
};

/**
 * \brief Locked waiter that used `async_mutex::async_lock()` to acquire the lock.
 **/
template <typename Token>
struct async_locked_waiter final: public locked_waiter {
    /**
     * \brief Constructs a new async_locked_waiter.
     * \param mutex A mutex that the waiter is trying to acquire a lock for.
     * \param next_waiter Pointer to the head of the waiters linked list to prepend this waiter to.
     * \param token The complention token to call when the asynchronous operation is completed.
     **/
    async_locked_waiter([[maybe_unused]] async_mutex *mutex, locked_waiter *next_waiter, Token &&token):
        locked_waiter(next_waiter), m_token(std::move(token)) {}

    void completion() override {
        std::cout << "waiter=" << this << " m_token()" << std::endl;
        m_token();
        std::cout << "waiter=" << this << " m_token() done" << std::endl;
    }

private:
    Token m_token; //!< The completion token to invoke when the lock is acquired.
};

/**
 * \brief Locked waiter that used `async_mutex::async_scoped_lock()` to acquire the lock.
 **/
template <typename Token>
struct scoped_async_locked_waiter final: public locked_waiter {
    /**
     * \brief Constructs a new scoped_async_locked_waiter.
     * \param mutex A mutex that the waiter is trying to acquire a lock for.
     * \param next_waiter Pointer to the head of the waiters linked list to prepend this waiter to.
     * \param token The complention token to call when the asynchronous operation is completed.
     **/
    scoped_async_locked_waiter(async_mutex *mutex, locked_waiter *next_waiter, Token &&token):
        locked_waiter(next_waiter), m_mutex(mutex), m_token(std::move(token)) {}

    void completion() override;

private:
    async_mutex *m_mutex; //!< The mutex whose lock is being awaited.
    Token m_token;        //!< The completion token to invoke when the lock is acquired.
};

/**
 * \brief An initiator for boost::asio::async_initiate().
 **/
template <template <typename Token> typename Waiter>
class async_lock_initiator_base {
public:
    /**
     * Constructs a new initiator for an operation on the given mutex.
     *
     * \param mutex A mutex on which the asynchronous lock operation is being initiated.
     **/
    explicit async_lock_initiator_base(async_mutex *mutex): m_mutex(mutex) {
        std::cout << "initiator=" << this << " constructor" << std::endl;
    }

    async_lock_initiator_base(const async_lock_initiator_base& o): m_mutex(o.m_mutex) {
        std::cout << "initiator=" << this << " copy=" << &o << std::endl;
    }

    async_lock_initiator_base(async_lock_initiator_base&& o): m_mutex(o.m_mutex) {
        std::cout << "initiator=" << this << " move=" << &o << std::endl;
    }

    ~async_lock_initiator_base() {
        std::cout << "initiator=" << this << " destructor" << std::endl;
    }
    /**
     * \brief Invoked by boost asio when the asynchronous operation is initiated.
     *
     * \param handler A completion handler (a callable) to be called when the asynchronous operation
     *                has completed (in our case, the lock has been acquired).
     * \tparam Handler A callable with signature void(T) where T is the type of the object that will be
     *                 returned as a result of `co_await`ing the operation. In our case that's either
     *                 `void` for `async_lock()` or `async_mutex_lock` for `async_scoped_lock()`.
     **/
    template <typename Handler>
    void operator()(Handler &&handler);

protected:
    async_mutex *m_mutex; //!< The mutex whose lock is being awaited.
};

/**
 * \brief Initiator for the async_lock() operation.
 **/
using initiate_async_lock = async_lock_initiator_base<async_locked_waiter>;

/**
 * \brief Initiator for the async_scoped_lock() operation.
 **/
using initiate_scoped_async_lock = async_lock_initiator_base<scoped_async_locked_waiter>;

} // namespace detail
/** \endinternal **/

/**
 * \brief A basic mutex that can acquire lock asynchronously using boost::asio coroutines.
 **/
class async_mutex {
public:
    /**
     * \brief Constructs a new unlocked mutex.
     **/
    async_mutex() noexcept = default;
    async_mutex(const async_mutex &) = delete;
    async_mutex(async_mutex &&) = delete;
    async_mutex &operator=(const async_mutex &) = delete;
    async_mutex &operator=(async_mutex &&) = delete;

    /**
     * \brief Destroys the mutex.
     *
     * \warning Destroying a mutex in locked state is undefined.
     **/
    ~async_mutex() {
        [[maybe_unused]] const auto state = m_state.load(std::memory_order_relaxed);
        assert(state == not_locked || state == locked_no_waiters);
        assert(m_waiters == nullptr);
    }

    /**
     * \brief Attempts to acquire lock without blocking.
     *
     * \return Returns `true` when the lock has been acquired, `false` when the
     *         lock is already held by someone else.
     * **/
    [[nodiscard]] bool try_lock() noexcept {
        auto old_state = not_locked;
        return m_state.compare_exchange_strong(old_state, locked_no_waiters, std::memory_order_acquire,
                                               std::memory_order_relaxed);
    }

    /**
     * \brief Asynchronously acquires as lock.
     *
     * When the returned awaitable is `co_await`ed it initiates the process
     * of acquiring a lock. The awaiter is suspended. Once the lock is acquired
     * (which can be immediately if nothing else holds the lock currently) the
     * awaiter is resumed and is now holding the lock.
     *
     * It's awaiter's responsibility to release the lock by calling `unlock()`.
     *
     * \param token A completion token (`boost::asio::use_awaitable`).
     * \tparam LockToken Type of the complention token.
     * \return An awaitable which will initiate the async operation when `co_await`ed.
     *         The result of `co_await`ing the awaitable is void.
     **/
#ifdef DOXYGEN
    template <typename LockToken>
    boost::asio::awaitable<> async_lock(LockToken &&token);
#else
    template <boost::asio::completion_token_for<void()> LockToken>
    [[nodiscard]] auto async_lock(LockToken&& token) {
        return boost::asio::async_initiate<LockToken, void()>(detail::initiate_async_lock(this), token);
    }
#endif

    /**
     * \brief Asynchronously acquires a lock and returns a scoped lock helper.
     *
     * Behaves exactly as `async_lock()`, except that the result of `co_await`ing the
     * returned awaitable is a scoped lock object, which will automatically release the
     * lock when destroyed.
     *
     * \param token A completion token (`boost::asio::use_awaitable`).
     * \tparam LockToken Type of the completion token.
     * \returns An awaitable which will initiate the async operation when `co_await`ed.
     *          The result of `co_await`ing the awaitable is `async_mutex_lock` holding
     *          the acquired lock.
     **/
#ifdef DOXYGEN
    template <typename LockToken>
    boost::asio::awaitable<async_mutex_lock> async_scoped_lock(LockToken &&token);
#else
    template <boost::asio::completion_token_for<void(async_mutex_lock)> LockToken>
    [[nodiscard]] auto async_scoped_lock(LockToken&& token) {
        return boost::asio::async_initiate<LockToken, void(async_mutex_lock)>(
            detail::initiate_scoped_async_lock(this), token);
    }
#endif

    template <class Executor> void unlock(const Executor &executor) {
        boost::asio::post(executor, [this]() { unlock(); });
    }

    /**
     * \brief Releases the lock.
     *
     * \warning Unlocking and already unlocked mutex is undefined.
     **/
    void unlock() {
        assert(m_state.load(std::memory_order_relaxed) != not_locked);

        auto *waiters_head = m_waiters;
        if (waiters_head == nullptr) {
            auto old_state = locked_no_waiters;
            // If old state was locked_no_waiters then transitions to not_locked and returns true,
            // otherwise do nothing and returns false.
            const bool released_lock = m_state.compare_exchange_strong(old_state, not_locked, std::memory_order_release,
                                                                       std::memory_order_relaxed);
            if (released_lock) {
                return;
            }

            // At least one new waiter. Acquire the list of new waiters atomically
            old_state = m_state.exchange(locked_no_waiters, std::memory_order_acquire);

            assert(old_state != locked_no_waiters && old_state != not_locked);

            // Transfer the list to m_waiters, reversing the list in the process
            // so that the head of the list is the first waiter to be resumed
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
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
        std::cout << "waiters_head=" << waiters_head << " completion" << std::endl;
        waiters_head->completion();
        std::cout << "waiters_head=" << waiters_head << " delete" << std::endl;
        delete waiters_head;
    }

private:
    template <template <typename Token> typename Waiter>
    friend class detail::async_lock_initiator_base;

    /**
     * \brief Indicates that the mutex is not locked.
     **/
    static constexpr std::uintptr_t not_locked = 1;
    /**
     * \brief Indicates that the mutex is locked, but no-one else is attempting to acquire the lock at the moment.
     **/
    static constexpr std::uintptr_t locked_no_waiters = 0;
    /**
     * \brief Holds the current state of the lock.
     *
     * The state can be `not_locked`, `locked_no_waiters` or a pointer to the head of a linked list
     * of new waiters (waiters who have attempted to acquire the lock since the last call to unlock().
     **/
    std::atomic<std::uintptr_t> m_state = {not_locked};
    /**
     * \brief Linked list of known locked waiters.
     **/
    detail::locked_waiter *m_waiters = nullptr;
};

/**
 * \brief A RAII-style lock for async_mutex which automatically unlocks the mutex when destroyed.
 **/
class async_mutex_lock {
public:
    /**
     * Constructs a new async_mutex_lock, taking ownership of the \c mutex.
     *
     * \param mutex Locked mutex to be unlocked when this objectis destroyed.
     *
     * \warning The \c mutex must be in a locked state.
     **/
    explicit async_mutex_lock(async_mutex &mutex, std::adopt_lock_t) noexcept: m_mutex(&mutex) {}

    /**
     * \brief Move constructor.
     * \param other The moved-from object.
     **/
    async_mutex_lock(async_mutex_lock &&other) noexcept: m_mutex(other.m_mutex) { other.m_mutex = nullptr; }

    async_mutex_lock &operator=(async_mutex_lock &&other) noexcept {
        m_mutex = std::exchange(other.m_mutex, nullptr);
        return *this;
    }

    /**
     * \brief Copy constructor (deleted).
     **/
    async_mutex_lock(const async_mutex_lock &) = delete;

    /**
     * \brief Copy assignment operator (deleted).
     **/
    async_mutex_lock &operator=(const async_mutex_lock &) = delete;

    ~async_mutex_lock() {
        if (m_mutex != nullptr) {
            m_mutex->unlock();
        }
    }

private:
    async_mutex *m_mutex; //!< The locked mutex being held by the scoped mutex lock.
};

/** \internal **/
namespace detail {

template <typename Token>
void scoped_async_locked_waiter<Token>::completion() {
    m_token(async_mutex_lock{*m_mutex, std::adopt_lock});
}

template <template <typename Token> typename Waiter>
template <typename Handler>
void async_lock_initiator_base<Waiter>::operator()(Handler &&handler) {
    std::cout << "initiator=" << this << " operator()" << std::endl;
    auto old_state = m_mutex->m_state.load(std::memory_order_acquire);
    std::unique_ptr<Waiter<Handler>> waiter;
    while (true) {
        if (old_state == async_mutex::not_locked) {
            if (m_mutex->m_state.compare_exchange_weak(old_state, async_mutex::locked_no_waiters,
                                                       std::memory_order_acquire, std::memory_order_relaxed))
            {
                // Lock acquired, resume the awaiter stright away
                std::cout << "initiator=" << this << " not_locked calling completion" << std::endl;
                Waiter(m_mutex, nullptr, std::forward<Handler>(handler)).completion();
                std::cout << "initiator=" << this << " not_locked" << std::endl;
                return;
            }
        } else {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            if (!waiter) {
                waiter.reset(new Waiter(m_mutex, reinterpret_cast<locked_waiter *>(old_state), std::forward<Handler>(handler)));
                std::cout << "initiator=" << this << " new waiter=" <<  waiter << std::endl;
            } else {
                waiter->next = reinterpret_cast<locked_waiter *>(old_state);
            }
            if (m_mutex->m_state.compare_exchange_weak(old_state, reinterpret_cast<std::uintptr_t>(waiter.release()),
                                                       std::memory_order_release, std::memory_order_relaxed))
            {
                std::cout << "initiator=" << this << " locked" << std::endl;
                return;
            }
        }
    }
}

} // namespace detail
/** \endinternal **/

} // namespace avast::asio
