#ifndef FIRISK_CORE_INSTRUMENT_BOND_HPP_INCLUDED
#define FIRISK_CORE_INSTRUMENT_BOND_HPP_INCLUDED

#include "firisk.h"
#include "../alloc.hpp"
#include "../containers.hpp"
#include "../time/calendar.hpp"
#include "../time/date.hpp"
#include "../time/daycount.hpp"
#include "../time/schedule.hpp"
#include "cashflow.hpp"

namespace firisk {

class Context;

/* A built bond: schedule and cashflows are computed once at
   construction and never recomputed. Every risk call reads them, and
   key-rate sweeps read them once per bump, so rebuilding here would put
   schedule generation inside the inner loop. */
class Bond {
public:
    Bond() noexcept;

    /* `def` has already been struct_size-validated by the shim.
       Writes a diagnostic into ctx on failure. */
    fir_status_t build(Context              &ctx,
                       const fir_bond_def_t &def,
                       const Calendar       &calendar) noexcept;

    const Schedule       &schedule() const noexcept  { return schedule_; }
    const Vector<Cashflow> &cashflows() const noexcept { return cashflows_; }

    size_t cashflow_count() const noexcept { return cashflows_.size(); }

    Date            valuation_date() const noexcept { return valuation_date_; }
    Date            maturity_date() const noexcept  { return maturity_; }
    double          face() const noexcept           { return face_; }
    double          coupon_rate() const noexcept    { return coupon_rate_; }
    fir_frequency_t frequency() const noexcept      { return frequency_; }
    fir_daycount_t  daycount() const noexcept       { return daycount_; }

    /* Coupons per year used to interpret a periodic yield. A zero-coupon
       bond has no coupon frequency, so annual is used as the quoting
       convention; callers wanting continuous should say so explicitly. */
    int yield_frequency() const noexcept
    {
        return frequency_ == FIR_FREQ_ZERO
                   ? 1
                   : static_cast<int>(frequency_);
    }

    /* Accrued interest per `face`, as of the valuation date. Zero when
       the valuation date is not inside a coupon period. */
    fir_status_t accrued(double *out_accrued) const noexcept;

private:
    FIR_IMPLEMENTS_ALLOCATORS;

    /* Fills cashflows_ from schedule_, dropping flows on or before the
       valuation date. Redemption is added to the final coupon. */
    fir_status_t build_cashflows(Context &ctx) noexcept;

    /* Coupon amount for period i, per `face`. */
    fir_status_t coupon_amount(size_t i, double *out_amount) const noexcept;

    Schedule         schedule_;
    Vector<Cashflow> cashflows_;

    Date            valuation_date_;
    Date            maturity_;
    double          face_;
    double          coupon_rate_;
    fir_frequency_t frequency_;
    fir_daycount_t  daycount_;
};

} /* namespace firisk */

#endif /* FIRISK_CORE_INSTRUMENT_BOND_HPP_INCLUDED */
