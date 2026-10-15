cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(triton_client LANGUAGES CXX)
    include(CTest)
endif()

find_package(Threads REQUIRED)

add_library(triton_client STATIC
    src/triton_client.cpp
)
set_target_properties(triton_client PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(triton_client PUBLIC cxx_std_17)
target_include_directories(triton_client PUBLIC include)
target_compile_options(triton_client PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(triton_client_tests tests/triton_client_tests.cpp)
    target_link_libraries(triton_client_tests PRIVATE
        triton_client Threads::Threads)
    target_compile_options(triton_client_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME triton_client_tests COMMAND triton_client_tests)
endif()
