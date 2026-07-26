cmake_minimum_required(VERSION 3.22.1)
project(openxr_bootstrap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenXR REQUIRED CONFIG)
add_subdirectory(
    ../../libs/xr_core
    "${CMAKE_CURRENT_BINARY_DIR}/xr_core"
)

set(native_app_glue_dir "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(android_native_app_glue STATIC
    "${native_app_glue_dir}/android_native_app_glue.c"
)
target_include_directories(android_native_app_glue PUBLIC "${native_app_glue_dir}")

add_library(openxr_bootstrap SHARED
    src/main/cpp/main.cpp
)
target_compile_definitions(openxr_bootstrap PRIVATE
    XR_USE_GRAPHICS_API_VULKAN
    XR_USE_PLATFORM_ANDROID
)
target_compile_options(openxr_bootstrap PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)
target_link_libraries(openxr_bootstrap PRIVATE
    xr_core
    android_native_app_glue
    android
)
target_link_options(openxr_bootstrap PRIVATE "-uANativeActivity_onCreate")
