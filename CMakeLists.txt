cmake_minimum_required(VERSION 3.25)

project(lush VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(LUSH_BUILD_TESTS "Build the test suite" ON)
option(LUSH_BUILD_CLI "Build the command-line frontend" ON)
option(LUSH_WERROR "Treat compiler warnings as errors" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor)
    if(LUSH_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_subdirectory(src)

if(LUSH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
