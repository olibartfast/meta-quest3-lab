cmake_minimum_required(VERSION 3.22.1)
project(vision_demo LANGUAGES CXX)

option(
    QUESTLAB_ENABLE_ONNXRUNTIME
    "Build the pinned ONNX Runtime inference backend"
    OFF
)
option(
    QUESTLAB_ENABLE_TRITON
    "Use the Triton Inference Server client backend (like tritonic)"
    ON
)
set(
    TritonClientBuild_DIR
    "$ENV{TritonClientBuild_DIR}"
    CACHE PATH
    "Triton client libraries (see tools/triton/extract_triton_libs.sh)"
)
if(NOT TritonClientBuild_DIR OR TritonClientBuild_DIR STREQUAL "")
    set(TritonClientBuild_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../build/triton_client_libs/install")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d videoio)

if(NOT TARGET rfdetr_inference_core)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../rfdetr_inference
        ${CMAKE_CURRENT_BINARY_DIR}/rfdetr_inference
    )
endif()
if(NOT TARGET monitor_corners)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../monitor_corners
        ${CMAKE_CURRENT_BINARY_DIR}/monitor_corners
    )
endif()
if(NOT TARGET quest_vision)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../libs/quest_vision
        ${CMAKE_CURRENT_BINARY_DIR}/quest_vision
    )
endif()

add_executable(vision_demo src/main.cpp)
target_compile_features(vision_demo PRIVATE cxx_std_17)
target_link_libraries(vision_demo PRIVATE
    rfdetr_inference_core
    monitor_corners
    quest_vision
    opencv_core opencv_imgproc opencv_calib3d opencv_videoio
)
target_compile_options(vision_demo PRIVATE -Wall -Wextra -Wpedantic)
