cmake_minimum_required(VERSION 3.21)

project(firisk
    VERSION 0.1.0
    DESCRIPTION "Fixed income risk library"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(FIRISK_BUILD_SHARED       "Build a shared library"      ON)
option(FIRISK_BUILD_TESTS        "Build the C smoke test"      ON)
option(FIRISK_BUILD_EXAMPLES     "Build the examples"          ON)
option(FIRISK_WARNINGS_AS_ERRORS "Treat warnings as errors"    OFF)
set(FIRISK_SANITIZER "" CACHE STRING "address, thread, memory, or empty")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

# Hidden by default on every platform, matching Windows behaviour and
# keeping the exported surface to what the map file names.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(CompilerWarnings)
include(Sanitizers)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(src)

if(FIRISK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/c_smoke)
endif()

if(FIRISK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS firisk
    EXPORT firiskTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES include/firisk.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT firiskTargets
    FILE firiskTargets.cmake
    NAMESPACE firisk::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/firisk)

configure_package_config_file(
    cmake/firiskConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/firiskConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/firisk)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/firiskConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/firiskConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/firiskConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/firisk)
