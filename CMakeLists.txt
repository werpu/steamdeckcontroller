cmake_minimum_required(VERSION 3.20)
project(steamdeckcontroller)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(steamdeckcontroller_core
        src/input_translation.cpp
        src/control_protocol.cpp)
target_include_directories(steamdeckcontroller_core PUBLIC include)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_sources(steamdeckcontroller_core PRIVATE src/controller_runtime_linux.cpp)
    target_link_libraries(steamdeckcontroller_core PUBLIC Threads::Threads)
    add_executable(steamdeckcontrollerd src/daemon.cpp)
    target_link_libraries(steamdeckcontrollerd PRIVATE steamdeckcontroller_core)
    install(TARGETS steamdeckcontrollerd RUNTIME DESTINATION bin)
endif ()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 gtk+-3.0)

if (GTK3_FOUND)
    add_executable(steamdeckcontroller main.cpp)
    target_include_directories(steamdeckcontroller PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_directories(steamdeckcontroller PRIVATE ${GTK3_LIBRARY_DIRS})
    target_compile_options(steamdeckcontroller PRIVATE ${GTK3_CFLAGS_OTHER})
    target_link_libraries(steamdeckcontroller PRIVATE steamdeckcontroller_core ${GTK3_LIBRARIES})
    install(TARGETS steamdeckcontroller RUNTIME DESTINATION bin)
else ()
    message(STATUS "gtk+-3.0 not found; steamdeckcontroller GUI target will not be built")
endif ()

include(CTest)
if (BUILD_TESTING)
    add_executable(input_translation_tests tests/input_translation_tests.cpp)
    target_link_libraries(input_translation_tests PRIVATE steamdeckcontroller_core)
    add_test(NAME input_translation_tests COMMAND input_translation_tests)

    add_executable(control_protocol_tests tests/control_protocol_tests.cpp)
    target_link_libraries(control_protocol_tests PRIVATE steamdeckcontroller_core)
    add_test(NAME control_protocol_tests COMMAND control_protocol_tests)
endif ()
