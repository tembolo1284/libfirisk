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
