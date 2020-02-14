# sandbox/CMakeLists.txt

find_package(ZLIB REQUIRED)

add_executable(sbox sandbox.c)
target_compile_features(sbox PUBLIC c_std_99)
target_link_libraries(sbox PUBLIC ZLIB::ZLIB)

add_executable(zinf zinf.c)
target_compile_features(zinf PUBLIC c_std_99)
target_link_libraries(zinf PUBLIC ZLIB::ZLIB)
