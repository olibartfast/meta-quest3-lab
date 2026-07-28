cmake_minimum_required(VERSION 3.22.1)
project(xr_spatial_anchors LANGUAGES CXX)

add_library(xr_spatial_anchors STATIC
    src/anchor_record.cpp
    src/meta_spatial_anchor_manager.cpp
)

set_target_properties(xr_spatial_anchors PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(xr_spatial_anchors PUBLIC cxx_std_17)
target_include_directories(xr_spatial_anchors PUBLIC include)
target_compile_options(xr_spatial_anchors PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)
target_link_libraries(xr_spatial_anchors PUBLIC xr_core)
