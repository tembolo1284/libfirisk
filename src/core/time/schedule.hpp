#ifndef FIRISK_CORE_TIME_SCHEDULE_HPP_INCLUDED
#define FIRISK_CORE_TIME_SCHEDULE_HPP_INCLUDED

#include "firisk.h"
#include "../alloc.hpp"
#include "../containers.hpp"
#include "calendar.hpp"
#include "date.hpp"
#include "daycount.hpp"

namespace firisk {

/* Accrual boundaries stay unadjusted; only the payment date moves under
   the business-day convention. This is the standard bond treatment —
   adjusting accrual dates as well is a swap convention and would change
   every accrued-interest number. */
struct Period {
    Date accrual_start;
    Date accrual_end;
    Date payment_date;
    bool is_stub;
};

struct ScheduleSpec {
    Date            effective_date;      /* accrual starts here        */
    Date            maturity_date;
    Date            first_coupon_date;   /* serial 0 = none            */
    fir_frequency_t frequency;
    fir_bdc_t       business_day_convention;
    bool            end_of_month;
};

class Schedule {
public:
    Schedule() noexcept : periods_(), frequency_(FIR_FREQ_ZERO) {}

    /* Generates backward from maturity. A first_coupon_date that is not
       on the regular grid produces a leading stub; a grid that does not
       land on effective_date produces one too. Both are flagged. */
    fir_status_t build(const ScheduleSpec &spec,
                       const Calendar     &calendar) noexcept;

    size_t size() const noexcept { return periods_.size(); }
    bool   empty() const noexcept { return periods_.empty(); }

    const Period &operator[](size_t i) const noexcept { return periods_[i]; }

    const Vector<Period> &periods() const noexcept { return periods_; }

    fir_frequency_t frequency() const noexcept { return frequency_; }

    /* The ACT/ACT ICMA denominator period for period `i`. For a regular
       period this is the period itself; for a stub it is the notional
       regular period the stub sits inside. */
    AccrualPeriod reference_period(size_t i) const noexcept;

    /* Index of the period containing `d`, or size() if none does. */
    size_t period_index_for(Date d) const noexcept;

private:
    FIR_IMPLEMENTS_ALLOCATORS;

    Vector<Period>  periods_;
    fir_frequency_t frequency_;
};

/* Shifts `d` by `months`, preserving end-of-month when `eom` is set and
   `d` is itself a month end. Exposed for schedule tests. */
Date shift_months(Date d, int months, bool eom) noexcept;

} /* namespace firisk */

#endif /* FIRISK_CORE_TIME_SCHEDULE_HPP_INCLUDED */
