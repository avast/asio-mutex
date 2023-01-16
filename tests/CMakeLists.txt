find_package(Catch2 REQUIRED)

add_executable(async_mutex_test)
target_sources(async_mutex_test PRIVATE
    async_mutex_test.cpp
)
target_link_libraries(async_mutex_test PRIVATE
    Catch2::Catch2WithMain
    asio_mutex
)

include(CTest)
include(Catch)
catch_discover_tests(async_mutex_test)
