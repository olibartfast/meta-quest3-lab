cmake_minimum_required(VERSION 3.22.1)
project(rfdetr_inference LANGUAGES CXX)

include(CTest)

option(
    QUESTLAB_ENABLE_ONNXRUNTIME
    "Build the pinned ONNX Runtime inference backend"
    OFF
)
option(
    QUESTLAB_ENABLE_TRITON
    "Build the Triton Inference Server client backend (like tritonic)"
    OFF
)
option(
    QUESTLAB_RFDETR_BUILD_TOOL
    "Build the host RF-DETR command-line executable"
    ON
)
set(
    TritonClientBuild_DIR
    "$ENV{TritonClientBuild_DIR}"
    CACHE PATH
    "Triton client libraries extracted from tritonserver-py3-sdk (see tools/triton/extract_triton_libs.sh)"
)
set(
    ONNXRUNTIME_ROOT
    ""
    CACHE PATH
    "Extracted onnxruntime-linux-x64-1.21.0 directory"
)

if(NOT TARGET artifact_integrity)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../libs/artifact_integrity
        ${CMAKE_CURRENT_BINARY_DIR}/artifact_integrity
    )
endif()
if(NOT TARGET camera_source)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../libs/camera_source
        ${CMAKE_CURRENT_BINARY_DIR}/camera_source
    )
endif()

add_library(rfdetr_inference_core STATIC
    src/detection_io.cpp
    src/json_value.cpp
    src/model_contract.cpp
    src/onnx_session.cpp
    src/postprocessing.cpp
    src/preprocessing.cpp
    src/timing.cpp
    src/triton_session.cpp
)

target_compile_features(rfdetr_inference_core PUBLIC cxx_std_17)
target_include_directories(rfdetr_inference_core PUBLIC include)
target_include_directories(rfdetr_inference_core PRIVATE src)
set_target_properties(rfdetr_inference_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(rfdetr_inference_core PUBLIC
    artifact_integrity
    camera_source
)
target_compile_options(rfdetr_inference_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(QUESTLAB_ENABLE_ONNXRUNTIME)
    if(NOT ONNXRUNTIME_ROOT)
        message(FATAL_ERROR
            "QUESTLAB_ENABLE_ONNXRUNTIME requires ONNXRUNTIME_ROOT; "
            "run tools/rfdetr_inference/prepare_onnxruntime.sh"
        )
    endif()
    if(ANDROID AND
       EXISTS "${ONNXRUNTIME_ROOT}/headers/onnxruntime_cxx_api.h" AND
       EXISTS "${ONNXRUNTIME_ROOT}/jni/${ANDROID_ABI}/libonnxruntime.so")
        set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/headers")
        set(ONNXRUNTIME_LIBRARY
            "${ONNXRUNTIME_ROOT}/jni/${ANDROID_ABI}/libonnxruntime.so"
        )
    else()
        find_path(
            ONNXRUNTIME_INCLUDE_DIR
            onnxruntime_cxx_api.h
            PATHS
                ${ONNXRUNTIME_ROOT}/include
                ${ONNXRUNTIME_ROOT}/headers
            NO_DEFAULT_PATH
            REQUIRED
        )
        find_library(
            ONNXRUNTIME_LIBRARY
            onnxruntime
            PATHS ${ONNXRUNTIME_ROOT}/lib
            NO_DEFAULT_PATH
            REQUIRED
        )
    endif()
    target_include_directories(
        rfdetr_inference_core PRIVATE ${ONNXRUNTIME_INCLUDE_DIR}
    )
    target_link_libraries(
        rfdetr_inference_core PRIVATE ${ONNXRUNTIME_LIBRARY}
    )
    target_compile_definitions(
        rfdetr_inference_core PRIVATE QUESTLAB_HAS_ONNXRUNTIME=1
    )
    set_target_properties(rfdetr_inference_core PROPERTIES
        BUILD_RPATH ${ONNXRUNTIME_ROOT}/lib
    )
endif()

if(QUESTLAB_ENABLE_TRITON)
    set(TritonClient_DIR "${TritonClientBuild_DIR}/lib/cmake/TritonClient")
    set(TritonCommon_DIR "${TritonClientBuild_DIR}/lib/cmake/TritonCommon")
    find_package(TritonCommon REQUIRED)
    find_package(TritonClient REQUIRED)
    target_include_directories(
        rfdetr_inference_core SYSTEM PRIVATE ${TritonClientBuild_DIR}/include
    )
    target_link_directories(
        rfdetr_inference_core PUBLIC ${TritonClientBuild_DIR}/lib
    )
    target_link_libraries(
        rfdetr_inference_core PUBLIC httpclient
    )
    # Same as tritonic: unoptimized builds against the 26.08+ SDK need
    # abseil CHECK helpers that the shared client libs do not export.
    set(QUESTLAB_TRITON_ABSL_LIBS)
    foreach(absl_lib log_internal_check_op leak_check strings
            strings_internal int128 raw_logging_internal throw_delegate base
            spinlock_wait log_severity)
        set(absl_path "${TritonClientBuild_DIR}/lib/libabsl_${absl_lib}.a")
        if(EXISTS "${absl_path}")
            list(APPEND QUESTLAB_TRITON_ABSL_LIBS "${absl_path}")
        endif()
    endforeach()
    if(QUESTLAB_TRITON_ABSL_LIBS)
        target_link_libraries(
            rfdetr_inference_core PRIVATE
            -Wl,--start-group ${QUESTLAB_TRITON_ABSL_LIBS} -Wl,--end-group
        )
    endif()
    target_compile_definitions(
        rfdetr_inference_core PRIVATE QUESTLAB_HAS_TRITON=1
    )
endif()

if(QUESTLAB_RFDETR_BUILD_TOOL)
    add_executable(rfdetr_inference src/main.cpp)
    target_link_libraries(rfdetr_inference PRIVATE rfdetr_inference_core)
    target_compile_options(rfdetr_inference PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    if(UNIX)
        add_executable(rfdetr_service src/service_main.cpp)
        target_link_libraries(rfdetr_service PRIVATE rfdetr_inference_core)
        target_compile_options(rfdetr_service PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endif()

if(BUILD_TESTING)
    add_executable(rfdetr_inference_tests tests/rfdetr_inference_tests.cpp)
    target_link_libraries(
        rfdetr_inference_tests PRIVATE rfdetr_inference_core
    )
    target_compile_definitions(rfdetr_inference_tests PRIVATE
        QUESTLAB_RFDETR_MANIFEST_PATH="${CMAKE_CURRENT_LIST_DIR}/../rfdetr_export/model-manifest.json"
    )
    target_compile_options(rfdetr_inference_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME rfdetr_inference_tests COMMAND rfdetr_inference_tests)
endif()
