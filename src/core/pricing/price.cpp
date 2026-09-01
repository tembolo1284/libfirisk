#include <cmath>
#include <limits>

#include "price.hpp"

namespace firisk {

namespace {

/* Discount factor and its first two yield derivatives. */
struct DfDerivs {
    double df;
    double d1;
    double d2;
};

DfDerivs df_derivs(double yield, double t, fir_compounding_t comp, int k) noexcept
{
    DfDerivs r{};

    switch (comp) {
    case FIR_COMP_SIMPLE: {
        const double base = 1.0 + yield * t;
        r.df = 1.0 / base;
        r.d1 = -t / (base * base);
        r.d2 = 2.0 * t * t / (base * base * base);
        break;
    }
    case FIR_COMP_CONTINUOUS: {
        r.df = std::exp(-yield * t);
        r.d1 = -t * r.df;
        r.d2 = t * t * r.df;
        break;
    }
    case FIR_COMP_PERIODIC:
    default: {
        const double kd   = static_cast<double>(k);
        const double base = 1.0 + yield / kd;
        r.df = std::pow(base, -kd * t);
        r.d1 = -t * std::pow(base, -kd * t - 1.0);
        r.d2 = t * (kd * t + 1.0) / kd * std::pow(base, -kd * t - 2.0);
        break;
    }
    }

    return r;
}

/* Longest cashflow time, used to locate the simple-compounding
   singularity at y = -1/t. */
double max_cashflow_time(const Bond &bond) noexcept
{
    double longest = 0.0;
    for (const Cashflow &cf : bond.cashflows()) {
        if (cf.time > longest)
            longest = cf.time;
    }
    return longest;
}

struct YieldSolveCtx {
    const Bond       *bond;
    fir_compounding_t comp;
    double            target_dirty;
};

void yield_objective(void *ud, double y, double *out_f, double *out_df) noexcept
{
    YieldSolveCtx *c = static_cast<YieldSolveCtx *>(ud);

    PvDerivatives d{};
    if (pv_derivatives(*c->bond, y, c->comp, &d) != FIR_OK) {
        *out_f  = std::numeric_limits<double>::quiet_NaN();
        *out_df = 0.0;
        return;
    }

    *out_f  = d.pv - c->target_dirty;
    *out_df = d.dpv;
}

struct SpreadSolveCtx {
    const Bond           *bond;
    const DiscountSource *curve;
    double                target_dirty;
};

double spread_objective(void *ud, double z) noexcept
{
    SpreadSolveCtx *c = static_cast<SpreadSolveCtx *>(ud);

    PriceComponents p{};
    if (price_from_curve_with_spread(*c->bond, *c->curve, z, &p) != FIR_OK)
        return std::numeric_limits<double>::quiet_NaN();

    return p.dirty - c->target_dirty;
}

} /* namespace */

fir_status_t pv_derivatives(const Bond       &bond,
                            double            yield,
                            fir_compounding_t comp,
                            PvDerivatives    *out) noexcept
{
    if (!out)
        return FIR_E_NULL_ARG;

    const int k = bond.yield_frequency();

    /* Periodic compounding is undefined at or below -k * 100%: the
       base goes non-positive and pow returns NaN rather than failing. */
    if (comp == FIR_COMP_PERIODIC &&
        !(1.0 + yield / static_cast<double>(k) > 0.0))
        return FIR_E_BAD_ARG;

    /* Simple discounting goes singular, then negative, at y = -1/t, and
       t differs per cashflow. Without this the PV silently stops being
       monotone in yield and any bracket built over it is meaningless.
       Only the longest flow needs checking: it reaches the singularity
       first as the yield falls. */
    if (comp == FIR_COMP_SIMPLE) {
        const double longest = max_cashflow_time(bond);
        if (!(1.0 + yield * longest > 0.0))
            return FIR_E_BAD_ARG;
    }

    double pv = 0.0, dpv = 0.0, d2pv = 0.0, twpv = 0.0;

    for (const Cashflow &cf : bond.cashflows()) {
        const DfDerivs d = df_derivs(yield, cf.time, comp, k);
        pv   += cf.amount * d.df;
        dpv  += cf.amount * d.d1;
        d2pv += cf.amount * d.d2;
        twpv += cf.amount * d.df * cf.time;
    }

    if (!std::isfinite(pv))
        return FIR_E_BAD_ARG;

    out->pv   = pv;
    out->dpv  = dpv;
    out->d2pv = d2pv;
    out->twpv = twpv;

    return FIR_OK;
}

fir_status_t price_from_yield(const Bond       &bond,
                              double            yield,
                              fir_compounding_t comp,
                              PriceComponents  *out) noexcept
{
    if (!out)
        return FIR_E_NULL_ARG;

    PvDerivatives d{};
    const fir_status_t rv = pv_derivatives(bond, yield, comp, &d);
    if (rv != FIR_OK)
        return rv;

    double accrued = 0.0;
    const fir_status_t arv = bond.accrued(&accrued);
    if (arv != FIR_OK)
        return arv;

    out->dirty   = d.pv;
    out->accrued = accrued;
    out->clean   = d.pv - accrued;

    return FIR_OK;
}

fir_status_t price_from_curve(const Bond           &bond,
                              const DiscountSource &curve,
                              PriceComponents      *out) noexcept
{
    return price_from_curve_with_spread(bond, curve, 0.0, out);
}

fir_status_t price_from_curve_with_spread(const Bond           &bond,
                                          const DiscountSource &curve,
                                          double                spread,
                                          PriceComponents      *out) noexcept
{
    if (!out)
        return FIR_E_NULL_ARG;

    double pv = 0.0;

    for (const Cashflow &cf : bond.cashflows()) {
        const double df = curve.discount(cf.time);

        /* A curve callback returning a non-finite or non-positive
           factor is a caller bug, but it would otherwise surface as a
           NaN price several layers away. */
        if (!std::isfinite(df) || df <= 0.0)
            return FIR_E_BAD_ARG;

        pv += cf.amount * df * (spread == 0.0
                                    ? 1.0
                                    : std::exp(-spread * cf.time));
    }

    if (!std::isfinite(pv))
        return FIR_E_BAD_ARG;

    double accrued = 0.0;
    const fir_status_t arv = bond.accrued(&accrued);
    if (arv != FIR_OK)
        return arv;

    out->dirty   = pv;
    out->accrued = accrued;
    out->clean   = pv - accrued;

    return FIR_OK;
}

fir_status_t yield_from_price(const Bond         &bond,
                              double              clean_price,
                              fir_compounding_t   comp,
                              const SolverConfig &cfg,
                              double             *out_yield) noexcept
{
    if (!out_yield)
        return FIR_E_NULL_ARG;

    double accrued = 0.0;
    const fir_status_t arv = bond.accrued(&accrued);
    if (arv != FIR_OK)
        return arv;

    YieldSolveCtx c;
    c.bond         = &bond;
    c.comp         = comp;
    c.target_dirty = clean_price + accrued;

    ScalarFnD fn;
    fn.eval      = yield_objective;
    fn.user_data = &c;

    /* The lower bound has to stay clear of each convention's
       singularity, and they sit in very different places. Periodic
       blows up at y = -k. Simple blows up at y = -1/t for the longest
       cashflow, which on a ten year bond is around -0.13 — far closer
       to zero than the -0.99 that suits continuous compounding. Using
       one bound for all three leaves the objective non-monotone over
       part of the range and the bracket test then means nothing. */
    const int k = bond.yield_frequency();
    double lo;

    switch (comp) {
    case FIR_COMP_SIMPLE: {
        const double longest = max_cashflow_time(bond);
        lo = longest > 0.0 ? -0.999 / longest : -0.99;
        break;
    }
    case FIR_COMP_PERIODIC:
        lo = -static_cast<double>(k) * 0.999;
        break;
    case FIR_COMP_CONTINUOUS:
    default:
        lo = -0.99;
        break;
    }

    /* Price is monotone decreasing in yield across the whole of
       [lo, hi] once the singularity is excluded, so a bracket exists
       for any target the instrument can actually reach. */
    const double hi = 10.0;

    return solve_newton(fn, 0.05, lo, hi, cfg, out_yield);
}

fir_status_t zspread_from_price(const Bond           &bond,
                                const DiscountSource &curve,
                                double                clean_price,
                                const SolverConfig   &cfg,
                                double               *out_spread) noexcept
{
    if (!out_spread)
        return FIR_E_NULL_ARG;

    double accrued = 0.0;
    const fir_status_t arv = bond.accrued(&accrued);
    if (arv != FIR_OK)
        return arv;

    SpreadSolveCtx c;
    c.bond         = &bond;
    c.curve        = &curve;
    c.target_dirty = clean_price + accrued;

    ScalarFn fn;
    fn.eval      = spread_objective;
    fn.user_data = &c;

    double a = 0.0, b = 0.0;
    const fir_status_t brv = bracket_root(fn, 0.0, 0.01, -0.5, 5.0, &a, &b);
    if (brv != FIR_OK)
        return brv;

    return solve_brent(fn, a, b, cfg, out_spread);
}

} /* namespace firisk */
