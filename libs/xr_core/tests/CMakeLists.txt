cmake_minimum_required(VERSION 3.22)
project(xr_core_tests LANGUAGES CXX)

enable_testing()

add_executable(xr_event_fanout_tests
    xr_event_fanout_tests.cpp
    ../src/xr_event_fanout.cpp
)
target_compile_features(xr_event_fanout_tests PRIVATE cxx_std_17)
target_include_directories(xr_event_fanout_tests PRIVATE
    stubs
    ../include
)
target_compile_options(xr_event_fanout_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME xr_event_fanout_tests COMMAND xr_event_fanout_tests)
