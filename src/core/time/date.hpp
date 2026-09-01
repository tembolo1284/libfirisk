#ifndef FIRISK_CORE_TIME_DATE_HPP_INCLUDED
#define FIRISK_CORE_TIME_DATE_HPP_INCLUDED

#include <compare>
#include <cstdint>

#include "firisk.h"

namespace firisk {

/* Hinnant's civil calendar algorithms, proleptic Gregorian.
   Day 0 is 1970-01-01. Valid well beyond any bond tenor. */

constexpr int32_t days_from_civil(int y, unsigned m, unsigned d) noexcept
{
    y -= m <= 2;
    const int      era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

struct civil_date {
    int      year;
    unsigned month;
    unsigned day;
};

constexpr civil_date civil_from_days(int32_t z) noexcept
{
    z += 719468;
    const int32_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe =
        (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int      y   = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp  = (5u * doy + 2u) / 153u;
    const unsigned d   = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m   = mp + (mp < 10u ? 3u : -9u);

    return civil_date{y + static_cast<int>(m <= 2u), m, d};
}

constexpr bool is_leap(int y) noexcept
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

constexpr unsigned days_in_month(int y, unsigned m) noexcept
{
    constexpr unsigned lengths[] =
        {0u, 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

    if (m == 2u && is_leap(y))
        return 29u;
    return (m >= 1u && m <= 12u) ? lengths[m] : 0u;
}

constexpr unsigned days_in_year(int y) noexcept
{
    return is_leap(y) ? 366u : 365u;
}

/* Value type. Stores the serial day internally so differencing and
   ordering are trivial, but converts to and from the ABI's YYYYMMDD
   at the boundary. Trivially copyable; never heap-allocated. */
class Date {
public:
    constexpr Date() noexcept : serial_(0) {}

    constexpr explicit Date(int32_t serial) noexcept : serial_(serial) {}

    constexpr Date(int y, unsigned m, unsigned d) noexcept
        : serial_(days_from_civil(y, m, d)) {}

    /* Caller must have checked is_valid_yyyymmdd first; this does not
       re-validate, so that pricing loops stay branch-free. */
    static constexpr Date from_yyyymmdd(fir_date_t date) noexcept
    {
        const int      y = static_cast<int>(date / 10000);
        const unsigned m = static_cast<unsigned>((date / 100) % 100);
        const unsigned d = static_cast<unsigned>(date % 100);
        return Date(y, m, d);
    }

    static constexpr bool is_valid_yyyymmdd(fir_date_t date) noexcept
    {
        if (date < 10000101 || date > 99991231)
            return false;

        const int      y = static_cast<int>(date / 10000);
        const unsigned m = static_cast<unsigned>((date / 100) % 100);
        const unsigned d = static_cast<unsigned>(date % 100);

        if (m < 1u || m > 12u)
            return false;
        return d >= 1u && d <= days_in_month(y, m);
    }

    constexpr fir_date_t to_yyyymmdd() const noexcept
    {
        const civil_date c = civil_from_days(serial_);
        return static_cast<fir_date_t>(c.year) * 10000
             + static_cast<fir_date_t>(c.month) * 100
             + static_cast<fir_date_t>(c.day);
    }

    constexpr int32_t  serial() const noexcept { return serial_; }
    constexpr int      year() const noexcept   { return civil_from_days(serial_).year; }
    constexpr unsigned month() const noexcept  { return civil_from_days(serial_).month; }
    constexpr unsigned day() const noexcept    { return civil_from_days(serial_).day; }

    /* 0 = Sunday through 6 = Saturday. */
    constexpr unsigned weekday() const noexcept
    {
        return static_cast<unsigned>(
            serial_ >= -4 ? (serial_ + 4) % 7 : (serial_ + 5) % 7 + 6);
    }

    constexpr bool is_weekend() const noexcept
    {
        const unsigned w = weekday();
        return w == 0u || w == 6u;
    }

    constexpr bool is_end_of_month() const noexcept
    {
        const civil_date c = civil_from_days(serial_);
        return c.day == days_in_month(c.year, c.month);
    }

    constexpr Date end_of_month() const noexcept
    {
        const civil_date c = civil_from_days(serial_);
        return Date(c.year, c.month, days_in_month(c.year, c.month));
    }

    constexpr Date add_days(int32_t n) const noexcept
    {
        return Date(serial_ + n);
    }

    /* Clamps to the last valid day: 31 Jan + 1 month is 28 or 29 Feb.
       Schedule generation layers end-of-month rolling on top of this. */
    constexpr Date add_months(int n) const noexcept
    {
        const civil_date c = civil_from_days(serial_);

        int total = c.year * 12 + static_cast<int>(c.month) - 1 + n;
        int y     = total / 12;
        int m     = total % 12;
        if (m < 0) {
            m += 12;
            --y;
        }

        const unsigned month = static_cast<unsigned>(m) + 1u;
        const unsigned last  = days_in_month(y, month);
        return Date(y, month, c.day < last ? c.day : last);
    }

    constexpr Date add_years(int n) const noexcept
    {
        return add_months(n * 12);
    }

    constexpr auto operator<=>(const Date &) const noexcept = default;
    constexpr bool operator==(const Date &) const noexcept = default;

    friend constexpr int32_t operator-(const Date &a, const Date &b) noexcept
    {
        return a.serial_ - b.serial_;
    }

private:
    int32_t serial_;
};

} /* namespace firisk */

#endif /* FIRISK_CORE_TIME_DATE_HPP_INCLUDED */
