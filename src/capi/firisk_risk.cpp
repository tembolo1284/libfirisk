#include "firisk.h"
#include "shim.hpp"
#include "../core/context.hpp"
#include "../core/instrument/bond.hpp"
#include "../core/pricing/discount.hpp"
#include "../core/pricing/price.hpp"
#include "../core/risk/analytic.hpp"
#include "../core/risk/curve_risk.hpp"

namespace {

/* The pricing entry points take no context, so they use fixed solver
   settings rather than the context's configured ones. */
firisk::SolverConfig default_solver() noexcept
{
    firisk::SolverConfig cfg;
    cfg.tolerance = 1e-12;
    cfg.max_iter  = 100;
    return cfg;
}

fir_status_t make_source(const fir_curve_t      *curve,
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

namespace {

enum CurveRiskField {
    kEffectiveDuration = 0,
    kEffectiveConvexity,
    kCurveDv01
};

fir_status_t curve_risk_field(fir_context_t     *ctx,
                              const fir_bond_t  *bond,
                              const fir_curve_t *curve,
                              double            *out,
                              CurveRiskField     which)
{
    FIR_ENTRY(ctx, c);
    FIR_RETURN_IF_NULL(c, FIR_E_NULL_ARG);
    FIR_REQUIRE_PTR(c, bond, "bond");
    FIR_REQUIRE_PTR(c, out, "out");

    firisk::DiscountSource src;
    const fir_status_t vrv = make_source(curve, &src);
    if (vrv != FIR_OK)
        return c->error().set(vrv, "curve: %s",
                              firisk::status_message(vrv));

    firisk::CurveRisk r{};
    const fir_status_t rv = firisk::curve_risk(
        *AS_CTYPE(firisk::Bond, bond), src, c->bump_size(), &r);
    if (rv != FIR_OK)
        return c->error().set(rv, "curve risk at a %g bp bump: %s",
                              c->bump_size(),
                              firisk::status_message(rv));

    switch (which) {
    case kEffectiveDuration:  *out = r.effective_duration;  break;
    case kEffectiveConvexity: *out = r.effective_convexity; break;
    case kCurveDv01:
    default:                  *out = r.dv01;                break;
    }

    return FIR_OK;
}

} /* namespace */

fir_status_t fir_bond_effective_duration(fir_context_t     *ctx,
                                         const fir_bond_t  *bond,
                                         const fir_curve_t *curve,
                                         double            *out_duration)
{
    return curve_risk_field(ctx, bond, curve, out_duration,
                            kEffectiveDuration);
}

fir_status_t fir_bond_effective_convexity(fir_context_t     *ctx,
                                          const fir_bond_t  *bond,
                                          const fir_curve_t *curve,
                                          double            *out_convexity)
{
    return curve_risk_field(ctx, bond, curve, out_convexity,
                            kEffectiveConvexity);
}

fir_status_t fir_bond_curve_dv01(fir_context_t     *ctx,
                                 const fir_bond_t  *bond,
                                 const fir_curve_t *curve,
                                 double            *out_dv01)
{
    return curve_risk_field(ctx, bond, curve, out_dv01, kCurveDv01);
}

fir_status_t fir_bond_key_rate_durations(fir_context_t     *ctx,
                                         const fir_bond_t  *bond,
                                         const fir_curve_t *curve,
                                         const double      *tenors,
                                         double            *out_krd,
                                         size_t             n)
{
    FIR_ENTRY(ctx, c);
    FIR_RETURN_IF_NULL(c, FIR_E_NULL_ARG);
    FIR_REQUIRE_PTR(c, bond, "bond");
    FIR_REQUIRE_PTR(c, tenors, "tenors");
    FIR_REQUIRE_PTR(c, out_krd, "out_krd");

    firisk::DiscountSource src;
    const fir_status_t vrv = make_source(curve, &src);
    if (vrv != FIR_OK)
        return c->error().set(vrv, "curve: %s",
                              firisk::status_message(vrv));

    const fir_status_t rv = firisk::key_rate_durations(
        *c, *AS_CTYPE(firisk::Bond, bond), src, tenors, out_krd, n,
        c->bump_size());
    if (rv != FIR_OK)
        return c->error().set(rv, "key rate durations at a %g bp bump: %s",
                              c->bump_size(), firisk::status_message(rv));

    return FIR_OK;
}

} /* extern "C" */
