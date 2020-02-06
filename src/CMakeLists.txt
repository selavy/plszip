# src/CMakeLists.txt

find_package(ZLIB REQUIRED)

add_executable(plzip plzip.cpp)
target_compile_features(plzip PUBLIC cxx_std_17)

add_executable(compress compress.cpp)
target_compile_features(compress PUBLIC cxx_std_17)
# target_link_libraries(compress PUBLIC ZLIB::ZLIB)

add_executable(inflate inflate.c)
target_compile_features(inflate PUBLIC c_std_99)

add_executable(gen_fixed_huffman gen_fixed_huffman.c)
target_compile_features(gen_fixed_huffman PUBLIC c_std_99)
