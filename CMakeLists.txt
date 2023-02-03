# SPDX-FileCopyrightText: 2023 Daniel Vrátil <daniel.vratil@gendigital.com>
#
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.10)
project(asio_mutex)

include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)

set(SANITIZER "" CACHE STRING "Specify which sanitizer to build with (supported: \"asan\", \"tsan\", \"ubsan\", \"msan\")")

option(BUILD_DOCS "Build documentation (requires Doxygen)" ON)
if (BUILD_DOCS)
    find_package(Doxygen COMPONENTS dot)
    set_package_properties(Doxygen PROPERTIES TYPE OPTIONAL)
endif()


add_library(asio-mutex-header INTERFACE)
target_compile_features(asio-mutex-header INTERFACE cxx_std_20)
target_include_directories(asio-mutex-header INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if (TARGET asio-uring-header)
    message(STATUS "Using Asio from asio-uring")
    target_link_libraries(asio-mutex-header INTERFACE asio-uring-header)
    set(ASIO_STANDALONE TRUE)
    target_compile_definitions(asio-mutex-header INTERFACE ASIO_STANDALONE)
else()
    message(STATUS "Using Boost Asio")
    find_package(Boost 1.80 REQUIRED)
    set(ASIO_STANDALONE FALSE)
    target_include_directories(asio-mutex-header INTERFACE
        $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}/boost>
    )
    target_compile_definitions(asio-mutex-header INTERFACE BOOST_ASIO_HAS_CO_AWAIT)
    target_link_libraries(asio-mutex-header INTERFACE Boost::headers)
endif()

if (BUILD_DOCS)
    add_subdirectory(doc)
endif()

if (ENABLE_TESTING)
    enable_testing()
    include(CTest)
    add_subdirectory(tests)
endif()

feature_summary(
    INCLUDE_QUIET_PACKAGES
    FATAL_ON_MISSING_REQUIRED_PACKAGES
    WHAT ENABLED_FEATURES DISABLED_FEATURES PACKAGES_FOUND PACKAGES_NOT_FOUND
)
