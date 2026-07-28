cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xr_hand_tracking LANGUAGES CXX)
    find_package(OpenXR REQUIRED CONFIG)
endif()

if(NOT TARGET xr_core)
    add_subdirectory(
        ../xr_core
        "${CMAKE_CURRENT_BINARY_DIR}/xr_core"
    )
endif()

add_library(xr_hand_tracking STATIC
    src/xr_hand_tracker.cpp
)

set_target_properties(xr_hand_tracking PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(xr_hand_tracking PUBLIC cxx_std_17)
target_include_directories(xr_hand_tracking PUBLIC include)
target_compile_options(xr_hand_tracking PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)
target_link_libraries(xr_hand_tracking PUBLIC xr_core)
