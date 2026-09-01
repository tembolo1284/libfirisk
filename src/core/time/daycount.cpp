#include "daycount.hpp"

namespace firisk {

namespace {

/* ICMA: the denominator is the actual length of the coupon period the
   accrual sits in, annualized by the coupon frequency. */
fir_status_t act_act_icma(Date                 start,
                          Date                 end,
                          const AccrualPeriod &period,
                          double              *out_yf) noexcept
{
    if (!period.valid())
        return FIR_E_BAD_SCHEDULE;

    /* An accrual reaching outside its own coupon period means the
       schedule and the accrual range disagree. Silently extrapolating
       here would produce accrued interest that looks reasonable. */
    if (start < period.period_start || end > period.period_end)
        return FIR_E_BAD_SCHEDULE;

    const double period_days =
        static_cast<double>(period.period_end - period.period_start);

    *out_yf = static_cast<double>(end - start)
            / (period_days * static_cast<double>(period.frequency));

    return FIR_OK;
}

} /* namespace */

double act_act_isda(Date start, Date end) noexcept
{
    if (end < start)
        return -act_act_isda(end, start);

    const int y1 = start.year();
    const int y2 = end.year();

    if (y1 == y2) {
        return static_cast<double>(end - start)
             / static_cast<double>(days_in_year(y1));
    }

    const Date start_year_end(y1 + 1, 1u, 1u);
    const Date end_year_start(y2, 1u, 1u);

    const double head = static_cast<double>(start_year_end - start)
                      / static_cast<double>(days_in_year(y1));
    const double tail = static_cast<double>(end - end_year_start)
                      / static_cast<double>(days_in_year(y2));

    return head + tail + static_cast<double>(y2 - y1 - 1);
}

fir_status_t year_fraction(fir_daycount_t       dc,
                           Date                 start,
                           Date                 end,
                           const AccrualPeriod &period,
                           double              *out_yf) noexcept
{
    if (!out_yf)
        return FIR_E_NULL_ARG;

    switch (dc) {
    case FIR_DC_ACT_360:
        *out_yf = static_cast<double>(end - start) / 360.0;
        return FIR_OK;

    case FIR_DC_ACT_365F:
        *out_yf = static_cast<double>(end - start) / 365.0;
        return FIR_OK;

    case FIR_DC_ACT_ACT_ISDA:
        *out_yf = act_act_isda(start, end);
        return FIR_OK;

    case FIR_DC_ACT_ACT_ICMA:
        return act_act_icma(start, end, period, out_yf);

    case FIR_DC_THIRTY_360_BOND:
        *out_yf = thirty_360_bond(start, end);
        return FIR_OK;

    case FIR_DC_THIRTY_E_360:
        *out_yf = thirty_e_360(start, end);
        return FIR_OK;

    default:
        return FIR_E_UNSUPPORTED;
    }
}

fir_status_t year_fraction(fir_daycount_t dc,
                           Date           start,
                           Date           end,
                           double        *out_yf) noexcept
{
    /* Default-constructed period is invalid, so ICMA reaches
       act_act_icma and fails there rather than guessing. */
    const AccrualPeriod none{};
    return year_fraction(dc, start, end, none, out_yf);
}

const char *daycount_name(fir_daycount_t dc) noexcept
{
    switch (dc) {
    case FIR_DC_ACT_360:           return "ACT/360";
    case FIR_DC_ACT_365F:          return "ACT/365F";
    case FIR_DC_ACT_ACT_ISDA:      return "ACT/ACT ISDA";
    case FIR_DC_ACT_ACT_ICMA:      return "ACT/ACT ICMA";
    case FIR_DC_THIRTY_360_BOND:   return "30/360 Bond";
    case FIR_DC_THIRTY_E_360:      return "30E/360";
    default:                       return "unknown";
    }
}

} /* namespace firisk */
