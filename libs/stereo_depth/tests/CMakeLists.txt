add_executable(stereo_depth_tests stereo_depth_tests.cpp)
target_link_libraries(stereo_depth_tests PRIVATE stereo_depth)
target_compile_options(stereo_depth_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME stereo_depth_tests COMMAND stereo_depth_tests)
