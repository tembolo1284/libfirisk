#include "firisk.h"
#include "shim.hpp"
#include "../core/instrument/bond.hpp"
#include "../core/pricing/discount.hpp"
#include "../core/pricing/price.hpp"
#include "../core/risk/analytic.hpp"
#include "../core/risk/curve_risk.hpp"

namespace {

/* Default solver settings for the context-free entry points. */
firisk::SolverConfig default_solver() noexcept
{
    firisk::SolverConfig cfg;
    cfg.tolerance = 1e-12;
    cfg.max_iter  = 100;
    return cfg;
}

fir_status_t make_source(const fir_curve_t     *curve,
                         firisk::DiscountSource *out) noexcept
{
    const fir_status_t rv = firisk::DiscountSource::validate(curve);
    if (rv != FIR_OK)
        return rv;

    *out = firisk::DiscountSource(curve);
    return FIR_OK;
}

} /* namespace */

extern "C" {

/* ---------------- pricing ---------------- */

fir_status_t fir_bond_price_from_yield(const fir_bond_t *bond,
                                       double            yield,
                                       fir_compounding_t comp,
                                       double           *out_clean_price)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    FIR_RETURN_IF_NULL(out_clean_price, FIR_E_NULL_ARG);

    firisk::PriceComponents p{};
    const fir_status_t rv = firisk::price_from_yield(
        *AS_CTYPE(firisk::Bond, bond), yield, comp, &p);
    if (rv != FIR_OK)
        return rv;

    *out_clean_price = p.clean;
    return FIR_OK;
}

fir_status_t fir_bond_yield_from_price(const fir_bond_t *bond,
                                       double            clean_price,
                                       fir_compounding_t comp,
                                       double           *out_yield)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    FIR_RETURN_IF_NULL(out_yield, FIR_E_NULL_ARG);

    return firisk::yield_from_price(*AS_CTYPE(firisk::Bond, bond),
                                    clean_price, comp,
                                    default_solver(), out_yield);
}

fir_status_t fir_bond_price_from_curve(const fir_bond_t  *bond,
                                       const fir_curve_t *curve,
                                       double            *out_clean_price)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    FIR_RETURN_IF_NULL(out_clean_price, FIR_E_NULL_ARG);

    firisk::DiscountSource src;
    const fir_status_t vrv = make_source(curve, &src);
    if (vrv != FIR_OK)
        return vrv;

    firisk::PriceComponents p{};
    const fir_status_t rv = firisk::price_from_curve(
        *AS_CTYPE(firisk::Bond, bond), src, &p);
    if (rv != FIR_OK)
        return rv;

    *out_clean_price = p.clean;
    return FIR_OK;
}

fir_status_t fir_bond_zspread(const fir_bond_t  *bond,
                              const fir_curve_t *curve,
                              double             clean_price,
                              double            *out_zspread)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    FIR_RETURN_IF_NULL(out_zspread, FIR_E_NULL_ARG);

    firisk::DiscountSource src;
    const fir_status_t vrv = make_source(curve, &src);
    if (vrv != FIR_OK)
        return vrv;

    return firisk::zspread_from_price(*AS_CTYPE(firisk::Bond, bond), src,
                                      clean_price, default_solver(),
                                      out_zspread);
}

/* ---------------- analytic risk ---------------- */

#define FIR_ANALYTIC_ACCESSOR(name, field)                                    \
    fir_status_t name(const fir_bond_t *bond,                                 \
                      double yield,                                           \
                      fir_compounding_t comp,                                 \
                      double *out)                                            \
    {                                                                         \
        FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);                             \
        FIR_RETURN_IF_NULL(out, FIR_E_NULL_ARG);                              \
                                                                              \
        firisk::AnalyticRisk r{};                                             \
        const fir_status_t rv = firisk::analytic_risk(                        \
            *AS_CTYPE(firisk::Bond, bond), yield, comp, &r);                  \
        if (rv != FIR_OK)                                                     \
            return rv;                                                        \
                                                                              \
        *out = r.field;                                                       \
        return FIR_OK;                                                        \
    }

FIR_ANALYTIC_ACCESSOR(fir_bond_macaulay_duration, macaulay_duration)
FIR_ANALYTIC_ACCESSOR(fir_bond_modified_duration, modified_duration)
FIR_ANALYTIC_ACCESSOR(fir_bond_convexity,         convexity)
FIR_ANALYTIC_ACCESSOR(fir_bond_dv01,              dv01)

#undef FIR_ANALYTIC_ACCESSOR

/* ---------------- curve risk ---------------- */

static fir_status_t curve_risk_field(const fir_bond_t  *bond,
                                     const fir_curve_t *curve,
                                     double            *out,
                                     int                which)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    FIR_RETURN_IF_NULL(out, FIR_E_NULL_ARG);

    firisk::DiscountSource src;
    const fir_status_t vrv = make_source(curve, &src);
    if (vrv != FIR_OK)
        return vrv;

    /* No context on these entry points, so the default 1bp bump is
       used rather than the context's configured size. */
    firisk::CurveRisk r{};
    const fir_status_t rv = firisk::curve_risk(
        *AS_CTYPE(firisk::Bond, bond), src, 1.0, &r);
    if (rv != FIR_OK)
        return rv;

    switch (which) {
    case 0: *out = r.effective_duration;  break;
    case 1: *out = r.effective_convexity; break;
    default: *out = r.dv01;               break;
    }

    return FIR_OK;
}

fir_status_t fir_bond_effective_duration(const fir_bond_t  *bond,
                                         const fir_curve_t *curve,
                                         double            *out_duration)
{
    return curve_risk_field(bond, curve, out_duration, 0);
}

fir_status_t fir_bond_effective_convexity(const fir_bond_t  *bond,
                                          const fir_curve_t *curve,
                                          double            *out_convexity)
{
    return curve_risk_field(bond, curve, out_convexity, 1);
}

fir_status_t fir_bond_curve_dv01(const fir_bond_t  *bond,
                                 const fir_curve_t *curve,
                                 double            *out_dv01)
{
    return curve_risk_field(bond, curve, out_dv01, 2);
}

fir_status_t fir_bond_key_rate_durations(const fir_bond_t  *bond,
                                         const fir_curve_t *curve,
                                         const double      *tenors,
                                         double            *out_krd,
                                         size_t             n)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);

    firisk::DiscountSource src;
    const fir_status_t vrv = make_source(curve, &src);
    if (vrv != FIR_OK)
        return vrv;

    firisk::Context scratch;
    return firisk::key_rate_durations(scratch,
                                      *AS_CTYPE(firisk::Bond, bond),
                                      src, tenors, out_krd, n, 1.0);
}

} /* extern "C" */
