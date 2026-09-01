#ifndef FIRISK_CORE_TIME_CALENDAR_HPP_INCLUDED
#define FIRISK_CORE_TIME_CALENDAR_HPP_INCLUDED

#include <algorithm>
#include <cstdint>

#include "firisk.h"
#include "../alloc.hpp"
#include "../containers.hpp"
#include "date.hpp"

namespace firisk {

/* Bit per weekday, 0 = Sunday .. 6 = Saturday. */
enum : uint8_t {
    kWeekendSatSun = (1u << 0) | (1u << 6),
    kWeekendFriSat = (1u << 5) | (1u << 6),
    kWeekendNone   = 0u
};

/* Holidays are supplied by the caller as a sorted, deduplicated array
   of serial days. The library never ships a holiday table: any table
   compiled into a .so is stale before the first calendar change, and
   updating one should not require a library release. */
class Calendar {
public:
    Calendar() noexcept : weekend_mask_(kWeekendSatSun), holidays_() {}

    /* Copies and normalizes: sorts, drops duplicates, drops entries
       that already fall on a weekend. Returns FIR_E_BAD_DATE if any
       input is not a valid YYYYMMDD. */
    fir_status_t set_holidays(const fir_date_t *dates, size_t n) noexcept;

    fir_status_t set_weekend_mask(uint8_t mask) noexcept
    {
        /* All seven days as weekend would make advance() never
           terminate. Six is already pathological but bounded. */
        if ((mask & 0x7fu) == 0x7fu)
            return FIR_E_BAD_ARG;
        weekend_mask_ = static_cast<uint8_t>(mask & 0x7fu);
        return FIR_OK;
    }

    uint8_t weekend_mask() const noexcept { return weekend_mask_; }
    size_t  holiday_count() const noexcept { return holidays_.size(); }

    bool is_weekend(Date d) const noexcept
    {
        return (weekend_mask_ >> d.weekday()) & 1u;
    }

    bool is_holiday(Date d) const noexcept
    {
        return std::binary_search(holidays_.begin(), holidays_.end(),
                                  d.serial());
    }

    bool is_business_day(Date d) const noexcept
    {
        return !is_weekend(d) && !is_holiday(d);
    }

    /* Next business day on or after `d`. */
    Date following(Date d) const noexcept
    {
        while (!is_business_day(d))
            d = d.add_days(1);
        return d;
    }

    /* Last business day on or before `d`. */
    Date preceding(Date d) const noexcept
    {
        while (!is_business_day(d))
            d = d.add_days(-1);
        return d;
    }

    /* Following, unless that crosses into the next month, then
       preceding. This is what keeps a schedule's coupon dates inside
       their intended months. */
    Date modified_following(Date d) const noexcept
    {
        const Date adjusted = following(d);
        if (adjusted.month() != d.month())
            return preceding(d);
        return adjusted;
    }

    Date adjust(Date d, fir_bdc_t bdc) const noexcept
    {
        switch (bdc) {
        case FIR_BDC_FOLLOWING:          return following(d);
        case FIR_BDC_PRECEDING:          return preceding(d);
        case FIR_BDC_MODIFIED_FOLLOWING: return modified_following(d);
        case FIR_BDC_NONE:
        default:                         return d;
        }
    }

    /* Move `n` business days from `d`; n may be negative. n == 0
       returns `d` unadjusted, matching settlement-lag conventions. */
    Date advance(Date d, int n) const noexcept
    {
        const int32_t step = n >= 0 ? 1 : -1;
        for (int i = 0; i != n; i += (n >= 0 ? 1 : -1)) {
            do {
                d = d.add_days(step);
            } while (!is_business_day(d));
        }
        return d;
    }

    int32_t business_days_between(Date start, Date end) const noexcept;

private:
    FIR_IMPLEMENTS_ALLOCATORS;

    uint8_t         weekend_mask_;
    Vector<int32_t> holidays_;
};

} /* namespace firisk */

#endif /* FIRISK_CORE_TIME_CALENDAR_HPP_INCLUDED */
