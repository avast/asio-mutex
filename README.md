# Async Mutex for Boost Asio

This repository contains an implementation of an asynchronous mutex. Asynchronous here means
that the locking operation can be `co_await`ed, thus avoiding potential deadlocks in multithreaded
coroutine code.

*Credits*: the implementation of the mutex itself is heavily inspired by Lewis Bakers's `async_mutex`
from [cppcoro](https://github.com/lewissbaker/cppcoro/), published under the MIT license.

## API

```cpp
namespace avast::asio {

/**
 * A basic mutex that can acquire lock asynchronously using coroutines.
 **/
class async_mutex {
public:
    /**
     * Constructs a new, unlocked mutex.
     **/
    explicit async_mutex() noexcept;

    /**
     * Destroys the mutex.
     *
     * The mutex must be in unlocked state, otherwise the behavior is undefined.
     **/
    ~async_mutex();

    /**
     * Attempts to acquire a lock without blocking.
     *
     * Returns `true` if lock has been acquired successfuly, `false` otherwise.
     **/
    bool try_lock();

    /**
     * Asynchronously acquires a lock.
     *
     * When the returned awaitable is `co_await`ed it initiates the process
     * of acquiring a lock. The awaiter is suspended. Once the lock is acquired
     * (which can be immediately if nothing else holds the lock currently) the
     * awaiter is resumed and is now holding the lock.
     *
     * It's awaiter's responsibility to release the lock by calling `unlock()`.
     **/
    boost::asio::awaitable<> lock_async(LockToken);

    /**
     * Asynchronously acquires a lock and returns a scoped lock.
     *
     * Behaves exactly like `lock_async()`, except that the result of `co_await`ing the
     * returned awaitable is a scoped lock object, which will automatically release the
     * lock when destroyed.
     **/
    boost::asio::awaitable<avast::asio::async_mutex_lock> scoped_lock_async(LockToken);


    /**
     * Unlocks the mutex.
     *
     * Calling this method on an unlocked mutex is undefined behavior.
     **/
    void unlock();
};

/**
 * A RAII-style lock for async_mutex which automatically unlocks the mutex when destroyed.
 **/
class async_mutex_lock {
public:
    /**
     * Constructs a new async_mutex_lock, taking ownership of the `mutex`.
     *
     * The mutex must be in a locked state.
     **/
    explicit async_mutex_lock(async_mutex &mutex, std::adopt_lock_t) noexcept;

    async_mutex_lock(async_mutex_lock &&) noexcept;

    async_mutex_lock(const async_mutex_lock &) = delete;
    async_mutex_lock &operator=(const async_mutex_lock &) = delete;

    /**
     * Unlocks the held mutex and destroys the lock.
     **/
    ~async_mutex_lock();
};
```

## Authors

* Daniel Vrátil <daniel.vratil@gendigital.com>
