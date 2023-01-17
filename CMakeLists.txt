cmake_minimum_required(VERSION 3.10)
project(asio_mutex)

include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(ENABLE_TESTING ON)

find_package(Boost 1.80 REQUIRED)
find_package(Doxygen COMPONENTS dot)
set_package_properties(Doxygen PROPERTIES TYPE OPTIONAL)

add_library(asio_mutex INTERFACE)
target_include_directories(asio_mutex INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(asio_mutex INTERFACE Boost::boost)

add_subdirectory(doc)
if (ENABLE_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(
    INCLUDE_QUIET_PACKAGES
    FATAL_ON_MISSING_REQUIRED_PACKAGES
    WHAT ENABLED_FEATURES DISABLED_FEATURES PACKAGES_FOUND PACKAGES_NOT_FOUND
)
