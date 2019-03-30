# src/CMakeLists.txt

add_executable(plzip
    plzip.cpp
    )
target_link_libraries(plzip PUBLIC
    Flags)
