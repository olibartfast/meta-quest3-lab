cmake_minimum_required(VERSION 3.22.1)
project(monitor_corners LANGUAGES CXX)

include(CTest)

find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)

add_library(monitor_corners STATIC
    src/monitor_corners.cpp
)
target_compile_features(monitor_corners PUBLIC cxx_std_17)
target_include_directories(monitor_corners PUBLIC include)
target_link_libraries(monitor_corners PUBLIC opencv_core opencv_imgproc)
target_link_libraries(monitor_corners PRIVATE opencv_calib3d)
target_compile_options(monitor_corners PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(monitor_corners_tests tests/monitor_corners_tests.cpp)
    target_link_libraries(monitor_corners_tests PRIVATE
        monitor_corners opencv_core opencv_imgproc opencv_calib3d)
    target_compile_options(monitor_corners_tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME monitor_corners_tests COMMAND monitor_corners_tests)
endif()
