# src/CMakeLists.txt

add_executable(plzip plzip.cpp)
target_compile_features(plzip PUBLIC cxx_std_17)

add_executable(compress compress.cpp)
target_compile_features(compress PUBLIC cxx_std_17)
