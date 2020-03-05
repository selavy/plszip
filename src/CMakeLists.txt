# src/CMakeLists.txt

find_package(ZLIB REQUIRED)

add_executable(compress compress.cpp)
target_compile_features(compress PUBLIC cxx_std_17)

add_executable(inflate plszip.cpp crc32.cpp inflate.cpp)
target_link_libraries(inflate
    PUBLIC
        ZLIB::ZLIB
    PRIVATE
        project_warnings
        cxx_project_options
)
