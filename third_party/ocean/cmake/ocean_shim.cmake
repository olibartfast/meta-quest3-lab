# Minimal Ocean shim: builds ocean_base/math/geometry as static libs.
# Usage: questlab_add_ocean() after setting OCEAN_SRC_DIR (defaults to
# <repo>/build/ocean-src, populated by third_party/ocean/fetch_ocean.sh).
# Ocean compiles as C++20; consumers stay on their own standard.
macro(questlab_add_ocean)
    if(NOT OCEAN_SRC_DIR)
        set(OCEAN_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../build/ocean-src")
    endif()
    if(NOT EXISTS "${OCEAN_SRC_DIR}/impl/ocean/geometry/RANSAC.h")
        message(FATAL_ERROR
            "Ocean sources missing at ${OCEAN_SRC_DIR}; "
            "run third_party/ocean/fetch_ocean.sh first")
    endif()
    if(ANDROID)
        set(OCEAN_PLATFORM_DEFINE OCEAN_PLATFORM_BUILD_ANDROID)
    elseif(WIN32)
        set(OCEAN_PLATFORM_DEFINE OCEAN_PLATFORM_BUILD_WINDOWS)
    else()
        set(OCEAN_PLATFORM_DEFINE OCEAN_PLATFORM_BUILD_LINUX)
    endif()
    foreach(ocean_module base math geometry)
        file(GLOB ocean_sources
            "${OCEAN_SRC_DIR}/impl/ocean/${ocean_module}/*.cpp")
        # jni helpers are Android-packaging only, not needed here.
        list(FILTER ocean_sources EXCLUDE REGEX ".*/jni/.*")
        add_library(ocean_${ocean_module} STATIC ${ocean_sources})
        target_include_directories(ocean_${ocean_module} PUBLIC
            "${OCEAN_SRC_DIR}/impl")
        target_compile_definitions(ocean_${ocean_module} PUBLIC
            ${OCEAN_PLATFORM_DEFINE})
        target_compile_features(ocean_${ocean_module} PUBLIC cxx_std_20)
    endforeach()
    find_package(Eigen3 REQUIRED)
    target_link_libraries(ocean_math PUBLIC Eigen3::Eigen)
    target_link_libraries(ocean_geometry PUBLIC ocean_base ocean_math)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(ocean_base PUBLIC Threads::Threads)
    endif()
endmacro()
