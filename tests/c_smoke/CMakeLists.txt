add_executable(fir_c_smoke smoke.c)

# Built as C99 against the installed-shape header only: this is the
# test that catches C++-isms leaking into firisk.h.
set_target_properties(fir_c_smoke PROPERTIES C_STANDARD 99)

target_link_libraries(fir_c_smoke PRIVATE firisk::firisk)

# The test calls exp() and fabs() itself; C does not pull in libm the
# way the C++ driver does.
find_library(FIRISK_LIBM m)
if(FIRISK_LIBM)
    target_link_libraries(fir_c_smoke PRIVATE ${FIRISK_LIBM})
endif()

add_test(NAME c_smoke COMMAND fir_c_smoke)
