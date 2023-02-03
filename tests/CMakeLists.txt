# SPDX-FileCopyrightText: 2023 Daniel Vrátil <daniel.vratil@gendigital.com>
#
# SPDX-License-Identifier: BSL-1.0

find_package(Catch2 REQUIRED)

add_executable(async_mutex_test)
target_sources(async_mutex_test PRIVATE
    async_mutex_test.cpp
)
target_link_libraries(async_mutex_test PRIVATE
    Catch2::Catch2WithMain
    asio-mutex-header
)

if (SANITIZER)
    if ("${SANITIZER}" STREQUAL "asan")
        target_compile_options(async_mutex_test PRIVATE -fsanitize=address -fno-omit-frame-pointer -fsanitize-address-use-after-scope)
    elseif ("${SANITIZER}" STREQUAL "tsan")
        target_compile_options(async_mutex_test PRIVATE -fsanitize=thread)
    elseif ("${SANITIZER}" STREQUAL "ubsan")
        target_compile_options(async_mutex_test PRIVATE -fsanitize=undefined)
    elseif ("${SANITIZER}" STREQUAL "msan")
        target_compile_options(async_mutex_test PRIVATE -fsanitize=memory)
    else ()
        message(FATAL_ERROR "Invalid SANITIZER value \"${SANITIZER}\", supported values: \"asan\", \"tsan\", \"ubsan\", \"msan\".")
    endif()
endif()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    target_compile_options(async_mutex_test PRIVATE -Wall -Wextra -Werror -pedantic)
endif()


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
