add_library(xr_core STATIC
    src/xr_error.cpp
    src/xr_instance.cpp
    src/xr_session.cpp
    src/vulkan_session_binding.cpp
)

set_target_properties(xr_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(xr_core PUBLIC
    include
)

target_compile_definitions(xr_core PUBLIC
    XR_USE_GRAPHICS_API_VULKAN
    XR_USE_PLATFORM_ANDROID
)

target_compile_options(xr_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)

target_link_libraries(xr_core PUBLIC
    OpenXR::openxr_loader
    log
    vulkan
)
