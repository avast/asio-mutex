find_package(Catch2 REQUIRED)

add_executable(async_mutex_test)
target_sources(async_mutex_test PRIVATE
    async_mutex_test.cpp
)
target_link_libraries(async_mutex_test PRIVATE
    Catch2::Catch2WithMain
    asio-mutex-header
)

include(Catch)
catch_discover_tests(async_mutex_test)

add_custom_target(check
    COMMAND echo [----] Running tests
    COMMAND ${CMAKE_CTEST_COMMAND} ${CMAKE_CTEST_ARGUMENTS}
    DEPENDS async_mutex_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)


add_custom_target(checkVerbose
    COMMAND echo [----] Running tests
    COMMAND ${CMAKE_CTEST_COMMAND} ${CMAKE_CTEST_ARGUMENTS} -V
    DEPENDS async_mutex_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
