cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pose_solver LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET xr_math)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../xr_math
        ${CMAKE_CURRENT_BINARY_DIR}/xr_math
    )
endif()

add_library(pose_solver STATIC
    src/pose_solver.cpp
)
set_target_properties(pose_solver PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(pose_solver PUBLIC cxx_std_17)
target_include_directories(pose_solver PUBLIC include)
target_link_libraries(pose_solver PUBLIC xr_math)
target_compile_options(pose_solver PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(
        pose_solver_tests
        tests/pose_solver_tests.cpp
    )
    target_link_libraries(pose_solver_tests PRIVATE pose_solver)
    target_compile_options(pose_solver_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME pose_solver_tests COMMAND pose_solver_tests)
endif()
