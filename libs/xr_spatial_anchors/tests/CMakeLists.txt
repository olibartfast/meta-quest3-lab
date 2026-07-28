cmake_minimum_required(VERSION 3.22)
project(xr_spatial_anchor_tests LANGUAGES CXX)

enable_testing()

add_executable(xr_spatial_anchor_record_tests
    anchor_record_tests.cpp
    ../src/anchor_record.cpp
)
target_compile_features(xr_spatial_anchor_record_tests PRIVATE cxx_std_17)
target_include_directories(xr_spatial_anchor_record_tests PRIVATE ../include)
target_compile_options(xr_spatial_anchor_record_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(
    NAME xr_spatial_anchor_record_tests
    COMMAND xr_spatial_anchor_record_tests
)
