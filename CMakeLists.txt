cmake_minimum_required(VERSION 3.16)

project(ThreadSafeQueueCourseDesign LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(queue_tests
    src/main.cpp
    src/ThreadSafeQueue.cpp
    tests/QueueTests.cpp
)

target_include_directories(queue_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(queue_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(queue_tests PRIVATE -Wall -Wextra -pedantic)
endif()
