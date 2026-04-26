cmake_minimum_required(VERSION 3.20)
project(steamdeckcontroller)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(steamdeckcontroller main.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

target_include_directories(steamdeckcontroller PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_directories(steamdeckcontroller PRIVATE ${GTK3_LIBRARY_DIRS})
target_compile_options(steamdeckcontroller PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_libraries(steamdeckcontroller PRIVATE ${GTK3_LIBRARIES})
