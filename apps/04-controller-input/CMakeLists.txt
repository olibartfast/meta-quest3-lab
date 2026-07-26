cmake_minimum_required(VERSION 3.22.1)
project(controller_input LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenXR REQUIRED CONFIG)
add_subdirectory(
    ../../libs/xr_core
    "${CMAKE_CURRENT_BINARY_DIR}/xr_core"
)
add_subdirectory(
    ../../libs/xr_interaction
    "${CMAKE_CURRENT_BINARY_DIR}/xr_interaction"
)
add_subdirectory(
    ../../libs/vulkan_renderer
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_renderer"
)

set(native_app_glue_dir "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(android_native_app_glue STATIC
    "${native_app_glue_dir}/android_native_app_glue.c"
)
target_include_directories(android_native_app_glue PUBLIC "${native_app_glue_dir}")

add_library(controller_input SHARED
    src/main/cpp/main.cpp
)

target_compile_definitions(controller_input PRIVATE
    XR_USE_GRAPHICS_API_VULKAN
    XR_USE_PLATFORM_ANDROID
)
if(QUEST_ENABLE_VULKAN_VALIDATION)
    target_compile_definitions(controller_input PRIVATE
        QUEST_ENABLE_VULKAN_VALIDATION=1
    )
endif()

target_compile_options(controller_input PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)

target_link_libraries(controller_input PRIVATE
    vulkan_renderer
    xr_interaction
    android_native_app_glue
    android
)

target_link_options(controller_input PRIVATE "-uANativeActivity_onCreate")
