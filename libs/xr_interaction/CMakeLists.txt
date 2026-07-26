cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xr_interaction LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET xr_math)
    add_subdirectory(
        ../xr_math
        "${CMAKE_CURRENT_BINARY_DIR}/xr_math"
    )
endif()

add_library(xr_interaction STATIC
    src/xr_interaction.cpp
)

set_target_properties(xr_interaction PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(xr_interaction PUBLIC cxx_std_17)
target_include_directories(xr_interaction PUBLIC include)
target_compile_options(xr_interaction PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
target_link_libraries(xr_interaction PUBLIC xr_math)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
