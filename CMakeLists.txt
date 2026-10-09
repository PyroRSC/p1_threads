# Convenience build for all four variants. Each variant folder also builds on its own.
cmake_minimum_required(VERSION 3.16)
project(prime_threads LANGUAGES CXX)

add_subdirectory(variant1_immediate_range)
add_subdirectory(variant2_deferred_range)
add_subdirectory(variant3_immediate_divisibility)
add_subdirectory(variant4_deferred_divisibility)
