add_library(BoostSML INTERFACE)
target_include_directories(BoostSML INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/sml/include)
target_compile_features(BoostSML INTERFACE cxx_std_17)

option(CXXOPTS_BUILD_EXAMPLES OFF)
option(CXXOPTS_BUILD_TESTS OFF)
option(CXXOPTS_ENABLE_INSTALL OFF)
add_subdirectory(cxxopts)
