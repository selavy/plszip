cmake_minimum_required(VERSION 3.5)

project(inflate C CXX)

add_library(project_warnings INTERFACE)
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

add_library(c_project_options INTERFACE)
add_library(cxx_project_options INTERFACE)
target_compile_features(c_project_options INTERFACE c_std_99)
target_compile_features(cxx_project_options INTERFACE cxx_std_17)
include(cmake/Sanitizers.cmake)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("-- enabling sanitizers")
    enable_sanitizers(c_project_options)
    enable_sanitizers(cxx_project_options)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")

find_package(ZLIB REQUIRED)
add_library(ZLIB2 INTERFACE)
target_compile_definitions(ZLIB2 INTERFACE NO_DUMMY_DECL)
target_link_libraries(ZLIB2 INTERFACE ZLIB::ZLIB)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(third_party)
add_subdirectory(src)
add_subdirectory(sandbox)
