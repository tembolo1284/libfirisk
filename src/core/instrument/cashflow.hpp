#ifndef FIRISK_CORE_INSTRUMENT_CASHFLOW_HPP_INCLUDED
#define FIRISK_CORE_INSTRUMENT_CASHFLOW_HPP_INCLUDED

#include "../time/date.hpp"

namespace firisk {

/* Internal cashflow. `time` is the year fraction from the valuation
   date under the discounting daycount, cached because every risk call
   reuses it; `payment_date` is retained so a curve can be queried by
   date rather than by interpolated time if it prefers. */
struct Cashflow {
    Date   payment_date;
    double time;
    double amount;

    /* Coupon periods this flow belongs to, kept for accrued interest
       and for ICMA year fractions. */
    size_t period_index;
};

} /* namespace firisk */

#endif /* FIRISK_CORE_INSTRUMENT_CASHFLOW_HPP_INCLUDED */
