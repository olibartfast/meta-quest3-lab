cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(stereo_depth LANGUAGES CXX)
    include(CTest)
endif()

add_library(stereo_depth STATIC
    src/stereo_depth.cpp
)

set_target_properties(stereo_depth PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(stereo_depth PUBLIC cxx_std_17)
target_include_directories(stereo_depth PUBLIC include)
target_compile_options(stereo_depth PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
