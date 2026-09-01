#include "bond.hpp"

#include "../context.hpp"

namespace firisk {

Bond::Bond() noexcept
    : schedule_()
    , cashflows_()
    , valuation_date_()
    , maturity_()
    , face_(0.0)
    , coupon_rate_(0.0)
    , frequency_(FIR_FREQ_ZERO)
    , daycount_(FIR_DC_ACT_ACT_ICMA)
{
}

fir_status_t Bond::build(Context              &ctx,
                         const fir_bond_def_t &def,
                         const Calendar       &calendar) noexcept
{
    if (ctx.valuation_date() == 0)
        return ctx.error().set(FIR_E_BAD_DATE,
                               "valuation date must be set on the context "
                               "before building an instrument");

    if (!Date::is_valid_yyyymmdd(def.issue_date))
        return ctx.error().set(FIR_E_BAD_DATE,
                               "issue_date %d is not a valid date",
                               static_cast<int>(def.issue_date));

    if (!Date::is_valid_yyyymmdd(def.maturity_date))
        return ctx.error().set(FIR_E_BAD_DATE,
                               "maturity_date %d is not a valid date",
                               static_cast<int>(def.maturity_date));

    if (def.first_coupon_date != 0 &&
        !Date::is_valid_yyyymmdd(def.first_coupon_date))
        return ctx.error().set(FIR_E_BAD_DATE,
                               "first_coupon_date %d is not a valid date",
                               static_cast<int>(def.first_coupon_date));

    if (!(def.face > 0.0))
        return ctx.error().set(FIR_E_BAD_ARG,
                               "face must be positive, got %g", def.face);

    /* Negative coupons exist. Absurd ones are almost always a decimal
       error: 4.5 passed where 0.045 was meant. */
    if (def.coupon_rate < -1.0 || def.coupon_rate > 1.0)
        return ctx.error().set(FIR_E_BAD_ARG,
                               "coupon_rate %g is outside [-1, 1]; rates "
                               "are decimals, not percentages",
                               def.coupon_rate);

    valuation_date_ = Date::from_yyyymmdd(ctx.valuation_date());
    maturity_       = Date::from_yyyymmdd(def.maturity_date);
    face_           = def.face;
    coupon_rate_    = def.coupon_rate;
    frequency_      = def.frequency;
    daycount_       = def.daycount;

    if (maturity_ <= valuation_date_)
        return ctx.error().set(FIR_E_BAD_SCHEDULE,
                               "maturity_date %d is not after the "
                               "valuation date %d",
                               static_cast<int>(def.maturity_date),
                               static_cast<int>(ctx.valuation_date()));

    ScheduleSpec spec;
    spec.effective_date          = Date::from_yyyymmdd(def.issue_date);
    spec.maturity_date           = maturity_;
    spec.first_coupon_date       = def.first_coupon_date
                                       ? Date::from_yyyymmdd(def.first_coupon_date)
                                       : Date();
    spec.frequency               = def.frequency;
    spec.business_day_convention = def.business_day_convention;
    spec.end_of_month            = def.end_of_month != 0;

    const fir_status_t rv = schedule_.build(spec, calendar);
    if (rv != FIR_OK)
        return ctx.error().set(rv,
                               "could not build a schedule from issue %d "
                               "to maturity %d at frequency %d: %s",
                               static_cast<int>(def.issue_date),
                               static_cast<int>(def.maturity_date),
                               static_cast<int>(def.frequency),
                               status_message(rv));

    return build_cashflows(ctx);
}

fir_status_t Bond::coupon_amount(size_t i, double *out_amount) const noexcept
{
    if (i >= schedule_.size())
        return FIR_E_BAD_SCHEDULE;

    if (frequency_ == FIR_FREQ_ZERO) {
        *out_amount = 0.0;
        return FIR_OK;
    }

    const Period &p = schedule_[i];

    double yf = 0.0;
    const fir_status_t rv = year_fraction(daycount_,
                                          p.accrual_start,
                                          p.accrual_end,
                                          schedule_.reference_period(i),
                                          &yf);
    if (rv != FIR_OK)
        return rv;

    *out_amount = face_ * coupon_rate_ * yf;
    return FIR_OK;
}

fir_status_t Bond::build_cashflows(Context &ctx) noexcept
{
    cashflows_.clear();
    cashflows_.reserve(schedule_.size());

    for (size_t i = 0; i < schedule_.size(); ++i) {
        const Period &p = schedule_[i];

        /* Flows paid on or before the valuation date belong to the
           previous holder. Comparison is on the payment date, so a
           coupon whose payment rolled past the valuation date is
           still ours. */
        if (p.payment_date <= valuation_date_)
            continue;

        double amount = 0.0;
        const fir_status_t rv = coupon_amount(i, &amount);
        if (rv != FIR_OK)
            return ctx.error().set(rv,
                                   "could not compute the coupon for "
                                   "period %zu under %s: %s",
                                   i, daycount_name(daycount_),
                                   status_message(rv));

        if (i + 1 == schedule_.size())
            amount += face_;

        /* Discounting time uses ACT/365F regardless of the coupon
           daycount: it measures elapsed time for the curve, not
           accrual, and mixing the two is a common source of small
           persistent pricing differences. */
        double t = 0.0;
        const fir_status_t trv = year_fraction(FIR_DC_ACT_365F,
                                               valuation_date_,
                                               p.payment_date,
                                               &t);
        if (trv != FIR_OK)
            return ctx.error().set(trv, "could not compute the discount "
                                        "time for period %zu", i);

        Cashflow cf;
        cf.payment_date = p.payment_date;
        cf.time         = t;
        cf.amount       = amount;
        cf.period_index = i;

        cashflows_.push_back(cf);
    }

    if (cashflows_.empty())
        return ctx.error().set(FIR_E_BAD_SCHEDULE,
                               "no cashflows remain after the valuation "
                               "date %d",
                               static_cast<int>(ctx.valuation_date()));

    /* Size the shared scratch buffers once, here, so no risk call
       allocates. */
    ctx.scratch_times().reserve(cashflows_.size());
    ctx.scratch_amounts().reserve(cashflows_.size());
    ctx.scratch_values().reserve(cashflows_.size());

    return FIR_OK;
}

fir_status_t Bond::accrued(double *out_accrued) const noexcept
{
    if (!out_accrued)
        return FIR_E_NULL_ARG;

    if (frequency_ == FIR_FREQ_ZERO) {
        *out_accrued = 0.0;
        return FIR_OK;
    }

    const size_t i = schedule_.period_index_for(valuation_date_);
    if (i >= schedule_.size()) {
        *out_accrued = 0.0;
        return FIR_OK;
    }

    const Period       &p   = schedule_[i];
    const AccrualPeriod ref = schedule_.reference_period(i);

    double yf = 0.0;
    const fir_status_t rv = year_fraction(daycount_,
                                          p.accrual_start,
                                          valuation_date_,
                                          ref,
                                          &yf);
    if (rv != FIR_OK)
        return rv;

    *out_accrued = face_ * coupon_rate_ * yf;
    return FIR_OK;
}

} /* namespace firisk */
