# src/CMakeLists.txt

add_executable(compress
    compress_tables.h
    crc32.cpp
    compress.cpp
    )
target_compile_features(compress PUBLIC cxx_std_17)
target_link_libraries(compress PRIVATE cxx_project_options cxxopts::cxxopts)

add_executable(inflate
    inflate_tables.h
    crc32.cpp
    plszip.cpp
    inflate.cpp
    )
target_link_libraries(inflate
    PUBLIC
    # ZLIB::ZLIB
        ZLIB2
    PRIVATE
        project_warnings
        cxx_project_options
)

add_executable(inflate_zlib
    inflate_tables.h
    crc32.cpp
    plszip.cpp
    inflate.cpp
    )
target_link_libraries(inflate_zlib
    PUBLIC
        # ZLIB::ZLIB
        ZLIB2
    PRIVATE
        project_warnings
        cxx_project_options
)
target_compile_definitions(inflate_zlib PRIVATE USE_ZLIB)
