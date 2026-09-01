#ifndef FIRISK_CORE_TIME_DAYCOUNT_HPP_INCLUDED
#define FIRISK_CORE_TIME_DAYCOUNT_HPP_INCLUDED

#include "firisk.h"
#include "date.hpp"

namespace firisk {

/* ACT/ACT ICMA is the odd one out: its denominator is the actual length
   of the coupon period the accrual sits in, times the frequency. Every
   other convention ignores this. Passing it unconditionally keeps one
   dispatch signature instead of two. */
struct AccrualPeriod {
    Date            period_start;
    Date            period_end;
    fir_frequency_t frequency;

    constexpr bool valid() const noexcept
    {
        return period_end > period_start && frequency > 0;
    }
};

/* Year fraction between two dates under `dc`.
   Returns FIR_OK and writes *out_yf, or a status without touching it.

   FIR_E_UNSUPPORTED if dc is not a known convention.
   FIR_E_BAD_SCHEDULE if dc is ACT/ACT ICMA and `period` is not valid,
   or the accrual range falls outside it. */
fir_status_t year_fraction(fir_daycount_t       dc,
                           Date                 start,
                           Date                 end,
                           const AccrualPeriod &period,
                           double              *out_yf) noexcept;

/* Convenience overload for the conventions that need no period. Calls
   the above with an invalid period, so ICMA fails loudly rather than
   silently producing a plausible-looking wrong number. */
fir_status_t year_fraction(fir_daycount_t dc,
                           Date           start,
                           Date           end,
                           double        *out_yf) noexcept;

/* True if `dc` needs a valid AccrualPeriod. Schedule building uses this
   to decide whether it must thread period context through. */
constexpr bool requires_period(fir_daycount_t dc) noexcept
{
    return dc == FIR_DC_ACT_ACT_ICMA;
}

const char *daycount_name(fir_daycount_t dc) noexcept;

/* --- pieces exposed for direct use and for unit testing --- */

/* 30/360 US bond basis (ISDA 2006 4.16(f)). Second date's day is only
   pulled back to 30 when the first date's day was already 30 or 31. */
constexpr double thirty_360_bond(Date start, Date end) noexcept
{
    const civil_date a = civil_from_days(start.serial());
    const civil_date b = civil_from_days(end.serial());

    unsigned d1 = a.day;
    unsigned d2 = b.day;

    if (d1 == 31u)
        d1 = 30u;
    if (d2 == 31u && d1 == 30u)
        d2 = 30u;

    return (360.0 * static_cast<double>(b.year - a.year)
          + 30.0 * (static_cast<double>(b.month) - static_cast<double>(a.month))
          + (static_cast<double>(d2) - static_cast<double>(d1))) / 360.0;
}

/* 30E/360 Eurobond basis. Both endpoints clamp to 30 unconditionally. */
constexpr double thirty_e_360(Date start, Date end) noexcept
{
    const civil_date a = civil_from_days(start.serial());
    const civil_date b = civil_from_days(end.serial());

    const unsigned d1 = a.day == 31u ? 30u : a.day;
    const unsigned d2 = b.day == 31u ? 30u : b.day;

    return (360.0 * static_cast<double>(b.year - a.year)
          + 30.0 * (static_cast<double>(b.month) - static_cast<double>(a.month))
          + (static_cast<double>(d2) - static_cast<double>(d1))) / 360.0;
}

/* ACT/ACT ISDA: split the range at calendar year boundaries and weight
   each piece by that year's own length. */
double act_act_isda(Date start, Date end) noexcept;

} /* namespace firisk */

#endif /* FIRISK_CORE_TIME_DAYCOUNT_HPP_INCLUDED */
