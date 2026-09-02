find_library(FIRISK_LIBM m)

add_executable(fir_price_bond price_bond.c)
target_link_libraries(fir_price_bond PRIVATE firisk::firisk)
set_target_properties(fir_price_bond PROPERTIES C_STANDARD 99)

add_executable(fir_bucket_risk bucket_risk.c)
target_link_libraries(fir_bucket_risk PRIVATE firisk::firisk)
set_target_properties(fir_bucket_risk PROPERTIES C_STANDARD 99)

if(FIRISK_LIBM)
    target_link_libraries(fir_price_bond  PRIVATE ${FIRISK_LIBM})
    target_link_libraries(fir_bucket_risk PRIVATE ${FIRISK_LIBM})
endif()

# The library links libasan under a sanitizer preset. An executable that
# is not itself instrumented gets libasan as a transitive dependency
# rather than first in the load order, and ASan refuses to start.
firisk_enable_sanitizers(fir_price_bond)
firisk_enable_sanitizers(fir_bucket_risk)
