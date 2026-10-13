cmake_minimum_required(VERSION 3.22.1)

# Isolated computer-vision shared library: bundles the portable detection
# pipeline (math, camera, projection, fusion, detector) into one
# quest_vision.so / quest_vision.dll instead of static archives scattered
# across consumers. XR/platform code (xr_core, depth_source, renderers)
# stays outside on purpose: this library must build on Android, host
# Linux, and Windows without an OpenXR runtime.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(quest_vision LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET artifact_integrity)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../artifact_integrity
        ${CMAKE_CURRENT_BINARY_DIR}/artifact_integrity
    )
endif()
if(NOT TARGET xr_math)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../xr_math
        ${CMAKE_CURRENT_BINARY_DIR}/xr_math
    )
endif()
if(NOT TARGET camera_source)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../camera_source
        ${CMAKE_CURRENT_BINARY_DIR}/camera_source
    )
endif()
if(NOT TARGET detection_projection)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../detection_projection
        ${CMAKE_CURRENT_BINARY_DIR}/detection_projection
    )
endif()
if(NOT TARGET detection_fusion)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../detection_fusion
        ${CMAKE_CURRENT_BINARY_DIR}/detection_fusion
    )
endif()
if(NOT TARGET object_detector)
    # Host inference tool stays off: the detector links its core library,
    # not the command-line executables.
    set(QUESTLAB_RFDETR_BUILD_TOOL OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../object_detector
        ${CMAKE_CURRENT_BINARY_DIR}/object_detector
    )
endif()

# Shared umbrella with the C ABI anchor plus the version symbol, so the
# SHARED target always produces quest_vision.so / quest_vision.dll.
add_library(quest_vision SHARED
    src/quest_vision.cpp
    src/quest_vision_c_api.cpp
)
set_target_properties(quest_vision PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME quest_vision
    VERSION 1.0.0
    SOVERSION 1
    # No __declspec annotations in the sources yet; export everything so
    # the Windows DLL is usable. Tighten to an explicit export macro later.
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_compile_features(quest_vision PUBLIC cxx_std_17)
target_include_directories(quest_vision PUBLIC include)
target_compile_definitions(quest_vision PRIVATE QUEST_VISION_BUILDING=1)
target_link_libraries(quest_vision
    PUBLIC
        artifact_integrity
        xr_math
        camera_source
        detection_projection
        detection_fusion
        object_detector
)
target_compile_options(quest_vision PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(
        quest_vision_c_api_tests
        tests/quest_vision_c_api_tests.cpp
    )
    target_link_libraries(
        quest_vision_c_api_tests PRIVATE quest_vision
    )
    target_compile_options(quest_vision_c_api_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(
        NAME quest_vision_c_api_tests
        COMMAND quest_vision_c_api_tests
    )
endif()
