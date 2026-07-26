add_library(xr_core STATIC
    src/xr_error.cpp
    src/xr_controller_actions.cpp
    src/xr_instance.cpp
    src/xr_session.cpp
    src/vulkan_session_binding.cpp
)

if(NOT TARGET xr_math)
    add_subdirectory(
        ../xr_math
        "${CMAKE_CURRENT_BINARY_DIR}/xr_math"
    )
endif()

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
    xr_math
    OpenXR::openxr_loader
    log
    vulkan
)
