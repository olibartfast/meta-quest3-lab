cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(screen_corners LANGUAGES CXX)
    include(CTest)
endif()

add_library(screen_corners STATIC
    src/screen_corners.cpp
)
set_target_properties(screen_corners PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(screen_corners PUBLIC cxx_std_17)
target_include_directories(screen_corners PUBLIC include)
target_compile_options(screen_corners PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(
        screen_corners_tests
        tests/screen_corners_tests.cpp
    )
    target_link_libraries(screen_corners_tests PRIVATE screen_corners)
    target_compile_options(screen_corners_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME screen_corners_tests COMMAND screen_corners_tests)
endif()
