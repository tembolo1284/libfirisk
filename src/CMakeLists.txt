set(FIRISK_SOURCES
    capi/firisk_version.cpp
    capi/firisk_alloc.cpp
    capi/firisk_error.cpp
    capi/firisk_context.cpp
    capi/firisk_calendar.cpp
    capi/firisk_bond.cpp
    capi/firisk_risk.cpp
    core/context.cpp
    core/time/calendar.cpp
    core/time/daycount.cpp
    core/time/schedule.cpp
    core/instrument/bond.cpp
    core/pricing/price.cpp
    core/pricing/solve.cpp
    core/risk/analytic.cpp
    core/risk/curve_risk.cpp)

if(FIRISK_BUILD_SHARED)
    add_library(firisk SHARED ${FIRISK_SOURCES})
    target_compile_definitions(firisk PRIVATE FIR_BUILD_SHARED)
else()
    add_library(firisk STATIC ${FIRISK_SOURCES})
    target_compile_definitions(firisk PUBLIC FIR_BUILD_STATIC)
endif()

add_library(firisk::firisk ALIAS firisk)

target_include_directories(firisk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(firisk PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

firisk_set_warnings(firisk)
firisk_enable_sanitizers(firisk)

if(FIRISK_BUILD_SHARED)
    if(APPLE)
        # No --exclude-libs on the Apple linker, so the symbol list is
        # what keeps statically linked dependencies out of our exports.
        target_link_options(firisk PRIVATE
            "-Wl,-exported_symbols_list,${CMAKE_SOURCE_DIR}/symbols/firisk.exported")
        set_target_properties(firisk PROPERTIES
            LINK_DEPENDS "${CMAKE_SOURCE_DIR}/symbols/firisk.exported")
    elseif(UNIX)
        target_link_options(firisk PRIVATE
            "-Wl,--version-script,${CMAKE_SOURCE_DIR}/symbols/firisk.map"
            "-Wl,--exclude-libs,ALL")
        set_target_properties(firisk PROPERTIES
            LINK_DEPENDS "${CMAKE_SOURCE_DIR}/symbols/firisk.map")
    endif()
endif()
