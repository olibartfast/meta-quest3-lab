cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xr_math LANGUAGES CXX)
    include(CTest)
endif()

add_library(xr_math STATIC
    src/xr_math.cpp
)

set_target_properties(xr_math PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(xr_math PUBLIC cxx_std_17)
target_include_directories(xr_math PUBLIC include)
target_compile_options(xr_math PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
