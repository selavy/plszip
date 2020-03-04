# src/CMakeLists.txt

find_package(ZLIB REQUIRED)

add_executable(plzip plzip.cpp)
target_link_libraries(plzip
    PRIVATE
    # project_warnings
        cxx_project_options
)

add_executable(compress compress.cpp)
target_compile_features(compress PUBLIC cxx_std_17)

add_executable(inflate
    pzlib.cpp
    inflate.cpp
    crc32.cpp
    )
target_link_libraries(inflate
    PUBLIC
        ZLIB::ZLIB
    PRIVATE
        project_warnings
        cxx_project_options
)

add_executable(gen_fixed_huffman gen_fixed_huffman.c)
target_compile_features(gen_fixed_huffman PUBLIC c_std_99)

add_executable(display_huffman_codes display_huffman_codes.cpp)
target_link_libraries(display_huffman_codes
    PRIVATE
    project_warnings
    cxx_project_options
)
