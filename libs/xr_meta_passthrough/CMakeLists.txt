add_library(xr_meta_passthrough STATIC
    src/meta_passthrough_fb.cpp
)

set_target_properties(xr_meta_passthrough PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(xr_meta_passthrough PUBLIC cxx_std_17)
target_include_directories(xr_meta_passthrough PUBLIC include)
target_compile_options(xr_meta_passthrough PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-missing-field-initializers
)
target_link_libraries(xr_meta_passthrough PUBLIC xr_core)
