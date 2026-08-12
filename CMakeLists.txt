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
    src/replacer.cpp
)

# TODO: add when test/bench sources exist
# add_executable(disk_manager_test tests/disk_manager_test.cpp)
# target_link_libraries(disk_manager_test tinydb_core)
# add_executable(disk_manager_bench tests/disk_manager_bench.cpp)
# target_link_libraries(disk_manager_bench tinydb_core)