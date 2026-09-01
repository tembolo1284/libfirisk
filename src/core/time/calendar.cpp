#include <algorithm>

#include "calendar.hpp"

namespace firisk {

fir_status_t Calendar::set_holidays(const fir_date_t *dates, size_t n) noexcept
{
    if (n && !dates)
        return FIR_E_NULL_ARG;

    Vector<int32_t> serials;
    serials.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        if (!Date::is_valid_yyyymmdd(dates[i]))
            return FIR_E_BAD_DATE;
        serials.push_back(Date::from_yyyymmdd(dates[i]).serial());
    }

    std::sort(serials.begin(), serials.end());
    serials.erase(std::unique(serials.begin(), serials.end()), serials.end());

    /* A holiday already falling on a weekend costs a binary-search hit
       on every business-day test and can never change an answer. */
    serials.erase(
        std::remove_if(serials.begin(), serials.end(),
                       [this](int32_t s) {
                           return is_weekend(Date(s));
                       }),
        serials.end());

    holidays_.swap(serials);
    return FIR_OK;
}

int32_t Calendar::business_days_between(Date start, Date end) const noexcept
{
    if (end < start)
        return -business_days_between(end, start);

    /* Half-open [start, end), matching the convention used for
       settlement lags and accrual day counts. */
    int32_t count = 0;
    for (Date d = start; d < end; d = d.add_days(1)) {
        if (is_business_day(d))
            ++count;
    }
    return count;
}

} /* namespace firisk */
