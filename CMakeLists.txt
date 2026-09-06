cmake_minimum_required(VERSION 3.16)
project(tinydb LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Supply a sensible default for single-configuration generators such as Ninja.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(
        CMAKE_BUILD_TYPE Debug
        CACHE STRING "Build type"
        FORCE
    )
endif()

set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel
)

# Sanitizers are independent of the build type.
set(
    TINYDB_SANITIZER "none"
    CACHE STRING "Runtime sanitizer"
)

set_property(
    CACHE TINYDB_SANITIZER
    PROPERTY STRINGS none undefined address-undefined thread
)

set(tinydb_supported_sanitizers
    none
    undefined
    address-undefined
    thread
)

if(NOT TINYDB_SANITIZER IN_LIST tinydb_supported_sanitizers)
    message(
        FATAL_ERROR
        "Unsupported TINYDB_SANITIZER='${TINYDB_SANITIZER}'. "
        "Expected one of: ${tinydb_supported_sanitizers}"
    )
endif()

option(
    TINYDB_ENABLE_PROFILING
    "Preserve frame pointers for profiling"
    OFF
)

# These options apply only to TinyDB targets, not downloaded dependencies.
add_library(tinydb_build_options INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        tinydb_build_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
    )
else()
    message(
        WARNING
        "TinyDB warning flags are not configured for "
        "${CMAKE_CXX_COMPILER_ID}"
    )
endif()

set(tinydb_sanitizer_flag "")

if(TINYDB_SANITIZER STREQUAL "undefined")
    set(tinydb_sanitizer_flag "-fsanitize=undefined")
elseif(TINYDB_SANITIZER STREQUAL "address-undefined")
    set(tinydb_sanitizer_flag "-fsanitize=address,undefined")
elseif(TINYDB_SANITIZER STREQUAL "thread")
    set(tinydb_sanitizer_flag "-fsanitize=thread")
endif()

if(tinydb_sanitizer_flag)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(
            FATAL_ERROR
            "Sanitizer configuration requires GCC or Clang"
        )
    endif()

    target_compile_options(
        tinydb_build_options
        INTERFACE
            "${tinydb_sanitizer_flag}"
            -fno-omit-frame-pointer
    )

    if(TINYDB_SANITIZER STREQUAL "undefined" OR TINYDB_SANITIZER STREQUAL "address-undefined")
        target_compile_options(
            tinydb_build_options
            INTERFACE
                -fno-sanitize-recover=undefined
        )
    endif()

    target_link_options(
        tinydb_build_options
        INTERFACE
            "${tinydb_sanitizer_flag}"
    )
endif()

if(TINYDB_ENABLE_PROFILING)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            tinydb_build_options
            INTERFACE
                -fno-omit-frame-pointer
        )
    else()
        message(
            FATAL_ERROR
            "Profiling options are not configured for "
            "${CMAKE_CXX_COMPILER_ID}"
        )
    endif()
endif()
 
add_library(tinydb_core STATIC
    src/disk_manager.cpp
    src/io_backend_buffered.cpp
    src/io_backend_direct.cpp
    src/io_backend_factory.cpp
    src/approx_lruk_replacer.cpp
    src/buffer_pool_management.cpp
    src/storage_manager.cpp
    src/table_page.cpp
)

target_compile_features(
    tinydb_core
    PUBLIC
        cxx_std_20
)

target_include_directories(
    tinydb_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    tinydb_core
    PRIVATE
        tinydb_build_options
)

# TODO: add when a bench source exists
# add_executable(disk_manager_bench tests/disk_manager_bench.cpp)
# target_link_libraries(disk_manager_bench tinydb_core)

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

function(add_tinydb_test test_name)
    add_executable(
        ${test_name}
        tests/${test_name}.cpp
    )

    target_link_libraries(
        ${test_name}
        PRIVATE
            tinydb_core
            tinydb_build_options
            GTest::gtest_main
    )

    gtest_discover_tests(
        ${test_name}
        DISCOVERY_TIMEOUT 60
    )
endfunction()

add_tinydb_test(byte_order_test)
add_tinydb_test(page_test)
add_tinydb_test(approx_lruk_replacer_test)
add_tinydb_test(io_backend_test)
add_tinydb_test(disk_manager_test)
add_tinydb_test(buffer_pool_manager_test)
add_tinydb_test(storage_manager_test)
add_tinydb_test(table_page_test)
