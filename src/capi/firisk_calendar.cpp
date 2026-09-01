#include "firisk.h"
#include "shim.hpp"
#include "../core/time/calendar.hpp"
#include "../core/time/date.hpp"

extern "C" {

fir_calendar_t *fir_calendar_new(void)
{
    return AS_TYPE(fir_calendar_t, new firisk::Calendar());
}

void fir_calendar_free(fir_calendar_t *cal)
{
    if (!cal)
        return;
    delete AS_TYPE(firisk::Calendar, cal);
}

fir_status_t fir_calendar_set_holidays(fir_calendar_t   *cal,
                                       const fir_date_t *dates,
                                       size_t            n)
{
    FIR_RETURN_IF_NULL(cal, FIR_E_NULL_ARG);
    return AS_TYPE(firisk::Calendar, cal)->set_holidays(dates, n);
}

fir_status_t fir_calendar_set_weekend(fir_calendar_t *cal, int weekend_mask)
{
    FIR_RETURN_IF_NULL(cal, FIR_E_NULL_ARG);

    if (weekend_mask < 0 || weekend_mask > 0x7f)
        return FIR_E_BAD_ARG;

    return AS_TYPE(firisk::Calendar, cal)
               ->set_weekend_mask(static_cast<uint8_t>(weekend_mask));
}

int fir_calendar_is_business_day(const fir_calendar_t *cal, fir_date_t date)
{
    if (!cal || !firisk::Date::is_valid_yyyymmdd(date))
        return 0;

    return AS_CTYPE(firisk::Calendar, cal)
               ->is_business_day(firisk::Date::from_yyyymmdd(date)) ? 1 : 0;
}

} /* extern "C" */
