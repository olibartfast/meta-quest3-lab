set(shader_tools_dir "${ANDROID_NDK}/shader-tools/linux-x86_64")
set(glslc "${shader_tools_dir}/glslc")
set(spirv_val "${shader_tools_dir}/spirv-val")
if(NOT EXISTS "${glslc}" OR NOT EXISTS "${spirv_val}")
    message(FATAL_ERROR
        "Pinned NDK shader tools were not found under ${shader_tools_dir}")
endif()

set(generated_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(vertex_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert")
set(fragment_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag")
set(debug_line_vertex_shader
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_lines.vert")
set(vertex_spv "${generated_shader_dir}/triangle.vert.spv")
set(fragment_spv "${generated_shader_dir}/triangle.frag.spv")
set(debug_line_vertex_spv
    "${generated_shader_dir}/debug_lines.vert.spv")
set(vertex_include "${generated_shader_dir}/triangle.vert.inc")
set(fragment_include "${generated_shader_dir}/triangle.frag.inc")
set(debug_line_vertex_include
    "${generated_shader_dir}/debug_lines.vert.inc")

add_custom_command(
    OUTPUT
        "${vertex_spv}"
        "${fragment_spv}"
        "${debug_line_vertex_spv}"
        "${vertex_include}"
        "${fragment_include}"
        "${debug_line_vertex_include}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_shader_dir}"
    COMMAND "${glslc}" --target-env=vulkan1.1 -O
        "${vertex_shader}" -o "${vertex_spv}"
    COMMAND "${glslc}" --target-env=vulkan1.1 -O
        "${fragment_shader}" -o "${fragment_spv}"
    COMMAND "${glslc}" --target-env=vulkan1.1 -O
        "${debug_line_vertex_shader}" -o "${debug_line_vertex_spv}"
    COMMAND "${spirv_val}" --target-env vulkan1.1 "${vertex_spv}"
    COMMAND "${spirv_val}" --target-env vulkan1.1 "${fragment_spv}"
    COMMAND "${spirv_val}" --target-env vulkan1.1
        "${debug_line_vertex_spv}"
    COMMAND "${glslc}" --target-env=vulkan1.1 -O -mfmt=c
        "${vertex_shader}" -o "${vertex_include}"
    COMMAND "${glslc}" --target-env=vulkan1.1 -O -mfmt=c
        "${fragment_shader}" -o "${fragment_include}"
    COMMAND "${glslc}" --target-env=vulkan1.1 -O -mfmt=c
        "${debug_line_vertex_shader}" -o "${debug_line_vertex_include}"
    DEPENDS
        "${vertex_shader}"
        "${fragment_shader}"
        "${debug_line_vertex_shader}"
    VERBATIM
)

add_library(vulkan_renderer STATIC
    src/vulkan_stereo_renderer.cpp
    "${vertex_include}"
    "${fragment_include}"
    "${debug_line_vertex_include}"
)

set_source_files_properties(
    "${vertex_include}"
    "${fragment_include}"
    "${debug_line_vertex_include}"
    PROPERTIES GENERATED TRUE
)

set_target_properties(vulkan_renderer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(vulkan_renderer
    PUBLIC include
    PRIVATE "${generated_shader_dir}"
)

target_compile_options(vulkan_renderer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)

target_link_libraries(vulkan_renderer PUBLIC
    xr_core
)
