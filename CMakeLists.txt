cmake_minimum_required(VERSION 3.16)
project(tinydb CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
 
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
 
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address,undefined")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")
 
add_compile_options(-Wall -Wextra)
 
include_directories(include)
 
add_library(tinydb_core
    src/disk_manager.cpp
    src/io_backend_buffered.cpp
    src/io_backend_direct.cpp
    src/io_backend_factory.cpp
    src/approx_lruk_replacer.cpp
    src/buffer_pool_management.cpp
    src/storage_manager.cpp
    src/table_page.cpp
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
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tinydb_core GTest::gtest_main)
    gtest_discover_tests(${test_name} DISCOVERY_TIMEOUT 60)
endfunction()

add_tinydb_test(byte_order_test)
add_tinydb_test(page_test)
add_tinydb_test(approx_lruk_replacer_test)
add_tinydb_test(io_backend_test)
add_tinydb_test(disk_manager_test)
add_tinydb_test(buffer_pool_manager_test)
add_tinydb_test(storage_manager_test)
add_tinydb_test(table_page_test)