cmake_minimum_required(VERSION 3.10)
project(asio_mutex)

include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(ENABLE_TESTING ON)

set(SANITIZER "" CACHE STRING "Specify which sanitizer to build with (supported: \"asan\", \"tsan\", \"ubsan\", \"msan\")")

find_package(Doxygen COMPONENTS dot)
set_package_properties(Doxygen PROPERTIES TYPE OPTIONAL)

add_library(asio-mutex-header INTERFACE)
target_compile_features(asio-mutex-header INTERFACE cxx_std_20)
target_include_directories(asio-mutex-header INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if (TARGET asio-uring-header)
    target_link_libraries(asio-mutex-header INTERFACE asio-uring-header)
else()
    target_include_directories(asio-mutex-header INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/external/asio/asio/include>"
    )
    target_compile_definitions(asio-mutex-header INTERFACE
        ASIO_HAS_CO_AWAIT ASIO_HAS_STD_COROUTINE ASIO_NO_DEPRECATED ASIO_HAS_STD_INVOKE_RESULT
        ASIO_STANDALONE ASIO_NO_TYPEID
    )
endif()

add_subdirectory(doc)

if (ENABLE_TESTING)
    if (SANITIZER)
        if ("${SANITIZER}" STREQUAL "asan")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -fsanitize-address-use-after-scope")
        elseif ("${SANITIZER}" STREQUAL "tsan")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
        elseif ("${SANITIZER}" STREQUAL "ubsan")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
        elseif ("${SANITIZER}" STREQUAL "msan")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory")
        else ()
            message(FATAL_ERROR "Invalid SANITIZER value \"${SANITIZER}\", supported values: \"asan\", \"tsan\", \"ubsan\", \"msan\".")
        endif()
    endif()

    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -pedantic ")
    endif()

    enable_testing()
    include(CTest)
    add_subdirectory(tests)
endif()

feature_summary(
    INCLUDE_QUIET_PACKAGES
    FATAL_ON_MISSING_REQUIRED_PACKAGES
    WHAT ENABLED_FEATURES DISABLED_FEATURES PACKAGES_FOUND PACKAGES_NOT_FOUND
)
