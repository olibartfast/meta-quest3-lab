add_executable(xr_interaction_tests
    xr_interaction_tests.cpp
)
target_link_libraries(xr_interaction_tests PRIVATE xr_interaction)
target_compile_options(xr_interaction_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
add_test(NAME xr_interaction_tests COMMAND xr_interaction_tests)
