find_package(ZLIB REQUIRED)

add_executable(gen_fixed_huffman gen_fixed_huffman.c)
target_link_libraries(gen_fixed_huffman PUBLIC c_project_options)

add_executable(display_huffman_codes display_huffman_codes.cpp)
target_link_libraries(display_huffman_codes
    PRIVATE
    project_warnings
    cxx_project_options
)
