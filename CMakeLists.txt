cmake_minimum_required(VERSION 3.20)

project(INTERCEPT VERSION 0.2 LANGUAGES CXX)

# ============================================================
# Platform
# ============================================================

if(NOT WIN32)
    message(FATAL_ERROR "INTERCEPT currently targets Windows only.")
endif()

# SA-MP / RakHook is 32-bit only.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "INTERCEPT must be built as 32-bit (Win32/x86).")
endif()

# ============================================================
# MSVC Runtime
# ============================================================

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime library"
        FORCE
    )
endif()

# ============================================================
# Dependencies
# ============================================================

include(FetchContent)

FetchContent_Declare(
    rakhook
    GIT_REPOSITORY https://github.com/imring/RakHook.git
    GIT_TAG 109daae3031cd030d272fa35407412443f03dede
)

FetchContent_MakeAvailable(rakhook)

# ============================================================
# INTERCEPT DLL
# ============================================================

add_library(INTERCEPT SHARED
    src/dllmain.cpp
    src/logger.cpp
    src/monitor.cpp
    src/decoder.cpp
    src/console.cpp
)

# ============================================================
# C++20
# ============================================================

target_compile_features(
    INTERCEPT
    PRIVATE
        cxx_std_20
)

# ============================================================
# Definitions
# ============================================================

target_compile_definitions(
    INTERCEPT
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        INTERCEPT_BUILD
)

# ============================================================
# Include directories
# ============================================================

target_include_directories(
    INTERCEPT
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================
# Libraries
# ============================================================

target_link_libraries(
    INTERCEPT
    PRIVATE
        rakhook::rakhook
)

# ============================================================
# Target
# ============================================================

set_target_properties(
    INTERCEPT
    PROPERTIES
        OUTPUT_NAME "INTERCEPT"
        CXX_EXTENSIONS OFF
)

# ============================================================
# Compiler
# ============================================================

if(MSVC)

    set_property(
        TARGET INTERCEPT
        PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    target_compile_options(
        INTERCEPT
        PRIVATE
            /W4
            /EHsc
        )

endif() 