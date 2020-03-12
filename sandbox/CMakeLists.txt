# sandbox/CMakeLists.txt

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
target_link_libraries(sml-first PUBLIC BoostSML cxx_project_options ZLIB2)

add_subdirectory(tools)
