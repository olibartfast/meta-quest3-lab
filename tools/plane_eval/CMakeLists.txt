cmake_minimum_required(VERSION 3.22.1)
project(plane_eval LANGUAGES CXX)

# Ocean spike: compares our RANSAC plane normal against ours + Ocean
# Huber refinement on identical noisy point sets. Ocean sources live in
# build/ocean-src (third_party/ocean/fetch_ocean.sh) and are never
# committed.

if(NOT TARGET detection_fusion)
    set(QUESTLAB_USE_OCEAN ON CACHE BOOL "" FORCE)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../libs/detection_fusion
        ${CMAKE_CURRENT_BINARY_DIR}/detection_fusion
    )
endif()

add_executable(plane_eval src/plane_eval.cpp)
target_include_directories(plane_eval PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../libs/detection_fusion/src
)
target_link_libraries(plane_eval PRIVATE detection_fusion ocean_geometry)
target_compile_features(plane_eval PRIVATE cxx_std_20)
target_compile_options(plane_eval PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME plane_eval COMMAND plane_eval)
