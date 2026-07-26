add_executable(xr_math_tests
    xr_math_tests.cpp
)

target_link_libraries(xr_math_tests PRIVATE xr_math)
target_compile_options(xr_math_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(NAME xr_math_tests COMMAND xr_math_tests)
