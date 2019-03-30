cmake_minimum_required(VERSION 3.5)

add_library(Flags INTERFACE)
target_compile_options(Flags INTERFACE
    -Wall
    -Werror
    -march=native
    # TODO: don't do this
    -std=c++17
    )

add_subdirectory(src)
