#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/detached.hpp>

#include <iostream>
#include <chrono>

#include "async_mutex.hpp"

#include <catch2/catch.hpp>

using namespace std::chrono_literals;

boost::asio::awaitable<void> scopedAsyncLockTest(boost::asio::io_context &ctx, avast::asio::async_mutex &mutex, int idx) {
    {
        std::cout << idx << " Locking" << std::endl;
        const auto lock = co_await mutex.scoped_lock_async(boost::asio::use_awaitable);
        std::cout << idx << " Locked" << std::endl;

        boost::asio::steady_timer timer{ctx};
        timer.expires_after(1s);
        co_await timer.async_wait(boost::asio::use_awaitable);

        std::cout << idx << " Unlocking" << std::endl;
    }
    std::cout << idx << " Unlocked" << std::endl;
}

TEST_CASE("scoped_lock_async") {
    boost::asio::io_context ioc;
    avast::asio::async_mutex mutex;
    boost::asio::co_spawn(ioc, scopedAsyncLockTest(ioc, mutex, 1), boost::asio::detached);
    boost::asio::co_spawn(ioc, scopedAsyncLockTest(ioc, mutex, 2), boost::asio::detached);
    ioc.run();
}

boost::asio::awaitable<void> asyncLockTest(boost::asio::io_context &ctx, avast::asio::async_mutex &mutex, int idx) {
    std::cout << idx << " Locking" << std::endl;
    co_await mutex.lock_async(boost::asio::use_awaitable);
    std::cout << idx << " Locked" << std::endl;

    boost::asio::steady_timer timer{ctx};
    timer.expires_after(1s);
    co_await timer.async_wait(boost::asio::use_awaitable);

    std::cout << idx << " Unlocking" << std::endl;
    mutex.unlock();
    std::cout << idx << " Unlocked" << std::endl;
}

TEST_CASE("lock_async") {
    boost::asio::io_context ioc;
    avast::asio::async_mutex mutex;
    boost::asio::co_spawn(ioc, asyncLockTest(ioc, mutex, 1), boost::asio::detached);
    boost::asio::co_spawn(ioc, asyncLockTest(ioc, mutex, 2), boost::asio::detached);
    ioc.run();
}

