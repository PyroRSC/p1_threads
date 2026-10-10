cmake_minimum_required(VERSION 3.16)
project(variant2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(variant2 main.cpp utils.hpp)
target_link_libraries(variant2 PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(variant2 PRIVATE /W4)
else()
  target_compile_options(variant2 PRIVATE -Wall -Wextra -Wpedantic)
endif()
