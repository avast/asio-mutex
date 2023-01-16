cmake_minimum_required(VERSION 3.10)
project(asio_mutex)

set(CMAKE_CXX_STANDARD 20)
set(ENABLE_TESTING ON)

find_package(Boost 1.80 REQUIRED)

add_library(asio_mutex INTERFACE)
target_include_directories(asio_mutex INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(asio_mutex INTERFACE Boost::boost)

if (ENABLE_TESTING)
    add_subdirectory(tests)
endif()

