# libfirisk Excel add-in.
#
# Windows and MSVC only. An XLL is a DLL that Excel loads directly, so
# it has to be built with the same toolchain and bitness as Excel
# itself — a WSL or mingw build will not load.
#
# Standalone from a Developer Command Prompt:
#   cmake -S bindings/excel/xll -B build/xll -A x64
#   cmake --build build/xll --config Release

cmake_minimum_required(VERSION 3.21)

project(firisk_xll
    VERSION 0.1.0
    DESCRIPTION "libfirisk Excel add-in"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR
        "The Excel add-in is Windows only. Configure this directory from "
        "a Visual Studio Developer Command Prompt, not from WSL.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR
        "The Excel add-in requires MSVC. Excel will not load a DLL built "
        "by mingw or clang-on-Windows without matching its runtime.")
endif()

# Excel's bitness has to match the add-in's. A 64-bit Excel cannot load
# a 32-bit XLL and the failure is silent — the add-in simply never
# appears. Configure with -A x64 (the default for VS generators).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING
        "Building a 32-bit add-in. This only works with 32-bit Excel; "
        "configure with -A x64 for the usual case.")
endif()

# ------------------------------------------------------------------
# the native library
# ------------------------------------------------------------------

# Built as a subproject and linked statically, so the add-in is one
# self-contained file the user can drop anywhere. A shared libfirisk
# would need to sit next to the XLL or on PATH, which is a support
# burden for no benefit.
if(NOT TARGET firisk)
    set(FIRISK_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(FIRISK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FIRISK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FIRISK_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../.." firisk_native)
endif()

# ------------------------------------------------------------------
# the add-in
# ------------------------------------------------------------------

add_library(firisk_xll SHARED
    firisk_xll.cpp
    marshal.cpp)

target_include_directories(firisk_xll PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(firisk_xll PRIVATE firisk)

set_target_properties(firisk_xll PROPERTIES
    OUTPUT_NAME "firisk"
    SUFFIX ".xll"          # what Excel looks for; otherwise a plain DLL
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

# The four entry points Excel resolves by name. __declspec(dllexport)
# in the source covers these, but naming them here means a rename or a
# typo fails the link rather than producing an add-in that loads and
# does nothing.
target_link_options(firisk_xll PRIVATE
    /EXPORT:xlAutoOpen
    /EXPORT:xlAutoClose
    /EXPORT:xlAutoFree12
    /EXPORT:xlAddInManagerInfo12)

target_compile_definitions(firisk_xll PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS)   # sscanf in the date parser

target_compile_options(firisk_xll PRIVATE
    /W4
    /permissive-
    /EHsc)

# Static CRT, so the add-in does not depend on the Visual C++
# redistributable being installed. Excel users are not developers and
# "install this runtime first" is a bad first experience.
set_property(TARGET firisk_xll PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET firisk PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ------------------------------------------------------------------
# install
# ------------------------------------------------------------------

install(TARGETS firisk_xll
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .)

# ------------------------------------------------------------------
# a convenience target that copies the built add-in somewhere useful
# ------------------------------------------------------------------

# Excel loads add-ins from anywhere, but the XLSTART folder loads them
# automatically at launch. Opt in rather than doing it by default.
option(FIRISK_XLL_AUTOLOAD
    "Copy the built add-in into Excel's XLSTART folder" OFF)

if(FIRISK_XLL_AUTOLOAD)
    add_custom_command(TARGET firisk_xll POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:firisk_xll>
            "$ENV{APPDATA}/Microsoft/Excel/XLSTART/firisk.xll"
        COMMENT "Copying firisk.xll into XLSTART")
endif()
