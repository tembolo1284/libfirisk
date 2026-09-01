cmake_minimum_required(VERSION 3.21)

project(firisk_python LANGUAGES CXX)

# Built standalone by scikit-build-core, so the native library is added
# from the parent directory rather than assumed to be installed.
if(NOT TARGET firisk)
    set(FIRISK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FIRISK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    # Static, so the extension is a single self-contained .so with no
    # rpath or LD_LIBRARY_PATH to get right at import time.
    set(FIRISK_BUILD_SHARED OFF CACHE BOOL "" FORCE)

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." firisk_native)
endif()

find_package(Python 3.9
    REQUIRED COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE nanobind_ROOT
    RESULT_VARIABLE nanobind_status)

if(NOT nanobind_status EQUAL 0)
    message(FATAL_ERROR
        "nanobind not found. pip install nanobind in the build environment.")
endif()

find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_firisk
    STABLE_ABI
    NB_STATIC
    firisk_ext.cpp)

target_link_libraries(_firisk PRIVATE firisk)

install(TARGETS _firisk LIBRARY DESTINATION firisk)
