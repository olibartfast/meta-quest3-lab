add_executable(camera_source_tests
    camera_source_tests.cpp
)
target_link_libraries(camera_source_tests PRIVATE camera_source)
target_compile_options(camera_source_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
add_test(NAME camera_source_tests COMMAND camera_source_tests)
