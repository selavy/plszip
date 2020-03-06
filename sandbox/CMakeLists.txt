# sandbox/CMakeLists.txt

find_package(ZLIB REQUIRED)

add_executable(sbox sandbox.c)
target_compile_features(sbox PUBLIC c_std_99)
target_link_libraries(sbox PUBLIC ZLIB::ZLIB)

add_executable(zinf zinf.c)
target_compile_features(zinf PUBLIC c_std_99)
target_link_libraries(zinf PUBLIC ZLIB::ZLIB)

add_executable(plzip plzip.cpp)
target_link_libraries(plzip
    PRIVATE
    # project_warnings
        cxx_project_options
)

add_executable(sml-hello-world sml_hello_world.cpp)
target_link_libraries(sml-hello-world PUBLIC BoostSML cxx_project_options)
    # project_warnings)

add_executable(sml-first sml_first.cpp)
target_link_libraries(sml-first PUBLIC BoostSML cxx_project_options ZLIB::ZLIB)
