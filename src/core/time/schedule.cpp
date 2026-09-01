#include <algorithm>

#include "schedule.hpp"

namespace firisk {

namespace {

int months_per_period(fir_frequency_t f) noexcept
{
    return f > 0 ? 12 / static_cast<int>(f) : 0;
}

bool frequency_ok(fir_frequency_t f) noexcept
{
    switch (f) {
    case FIR_FREQ_ZERO:
    case FIR_FREQ_ANNUAL:
    case FIR_FREQ_SEMIANNUAL:
    case FIR_FREQ_QUARTERLY:
    case FIR_FREQ_MONTHLY:
        return true;
    default:
        return false;
    }
}

} /* namespace */

Date shift_months(Date d, int months, bool eom) noexcept
{
    const Date shifted = d.add_months(months);

    /* Only anchor to month end if the source date was a month end.
       Otherwise 30 Jun would wrongly become 31 Dec under a 6m step. */
    if (eom && d.is_end_of_month())
        return shifted.end_of_month();

    return shifted;
}

fir_status_t Schedule::build(const ScheduleSpec &spec,
                             const Calendar     &calendar) noexcept
{
    if (!frequency_ok(spec.frequency))
        return FIR_E_UNSUPPORTED;

    if (spec.maturity_date <= spec.effective_date)
        return FIR_E_BAD_SCHEDULE;

    periods_.clear();
    frequency_ = spec.frequency;

    /* Zero-coupon: one period, no intermediate dates. */
    if (spec.frequency == FIR_FREQ_ZERO) {
        Period p;
        p.accrual_start = spec.effective_date;
        p.accrual_end   = spec.maturity_date;
        p.payment_date  = calendar.adjust(spec.maturity_date,
                                          spec.business_day_convention);
        p.is_stub       = false;
        periods_.push_back(p);
        return FIR_OK;
    }

    const int  months = months_per_period(spec.frequency);
    const bool eom    = spec.end_of_month;

    const bool has_first_coupon = spec.first_coupon_date.serial() != 0;

    if (has_first_coupon) {
        if (spec.first_coupon_date <= spec.effective_date ||
            spec.first_coupon_date > spec.maturity_date)
            return FIR_E_BAD_SCHEDULE;
    }

    /* Backward generation stops at the first coupon date when one is
       given, otherwise at the effective date. */
    const Date stop = has_first_coupon ? spec.first_coupon_date
                                       : spec.effective_date;

    Vector<Date> boundaries;
    boundaries.push_back(spec.maturity_date);

    Date cursor = spec.maturity_date;
    for (;;) {
        const Date prev = shift_months(cursor, -months, eom);

        /* Guard against a degenerate step that fails to move backward. */
        if (prev >= cursor)
            return FIR_E_BAD_SCHEDULE;

        if (prev <= stop)
            break;

        boundaries.push_back(prev);
        cursor = prev;
    }

    boundaries.push_back(stop);

    /* With an explicit first coupon date, the accrual still starts at
       the effective date, so the leading stub is added on top. */
    if (has_first_coupon && spec.effective_date < stop)
        boundaries.push_back(spec.effective_date);

    std::reverse(boundaries.begin(), boundaries.end());

    periods_.reserve(boundaries.size() - 1);

    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        Period p;
        p.accrual_start = boundaries[i];
        p.accrual_end   = boundaries[i + 1];
        p.payment_date  = calendar.adjust(p.accrual_end,
                                          spec.business_day_convention);

        /* A period is regular exactly when stepping one frequency back
           from its end reproduces its start. */
        p.is_stub = shift_months(p.accrual_end, -months, eom)
                        != p.accrual_start;

        periods_.push_back(p);
    }

    if (periods_.empty())
        return FIR_E_BAD_SCHEDULE;

    return FIR_OK;
}

AccrualPeriod Schedule::reference_period(size_t i) const noexcept
{
    AccrualPeriod ref{};

    if (i >= periods_.size() || frequency_ == FIR_FREQ_ZERO)
        return ref;

    const Period &p = periods_[i];
    ref.frequency   = frequency_;

    if (!p.is_stub) {
        ref.period_start = p.accrual_start;
        ref.period_end   = p.accrual_end;
        return ref;
    }

    /* For a stub, ICMA measures against the notional regular period it
       sits inside, constructed by stepping back from the period end.
       A long stub therefore yields a year fraction above 1/frequency,
       which is the intended behaviour. */
    const int months = months_per_period(frequency_);
    ref.period_start = shift_months(p.accrual_end, -months,
                                    p.accrual_end.is_end_of_month());
    ref.period_end   = p.accrual_end;

    return ref;
}

size_t Schedule::period_index_for(Date d) const noexcept
{
    for (size_t i = 0; i < periods_.size(); ++i) {
        if (d >= periods_[i].accrual_start && d < periods_[i].accrual_end)
            return i;
    }
    return periods_.size();
}

} /* namespace firisk */
