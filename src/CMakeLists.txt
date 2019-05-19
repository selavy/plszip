# src/CMakeLists.txt

add_executable(plzip plzip.cpp)
target_link_libraries(plzip PUBLIC Flags)

add_executable(compress compress.cpp)
target_link_libraries(compress PUBLIC Flags)
