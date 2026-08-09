cmake_minimum_required(VERSION 3.20)

project(INTERCEPT VERSION 0.2.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "INTERCEPT currently targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "INTERCEPT must be built as 32-bit (Win32/x86).")
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime library" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    rakhook
    GIT_REPOSITORY https://github.com/imring/RakHook.git
    GIT_TAG 109daae3031cd030d272fa35407412443f03dede
)
FetchContent_MakeAvailable(rakhook)

add_library(INTERCEPT SHARED
    src/dllmain.cpp
    src/logger.cpp
    src/monitor.cpp
    src/decoder.cpp
    src/console.cpp
    src/event_queue.cpp
    src/session.cpp
    src/gui.cpp
)

target_compile_features(INTERCEPT PRIVATE cxx_std_20)

target_compile_definitions(INTERCEPT PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    INTERCEPT_BUILD
)

target_include_directories(INTERCEPT PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(INTERCEPT PRIVATE
    rakhook::rakhook
    comctl32
)

set_target_properties(INTERCEPT PROPERTIES
    OUTPUT_NAME "INTERCEPT"
    CXX_EXTENSIONS OFF
)

if(MSVC)
    set_property(TARGET INTERCEPT PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(INTERCEPT PRIVATE /W4 /EHsc)
endif()