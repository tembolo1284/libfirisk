#include <cmath>

#include "curve_risk.hpp"

#include "../context.hpp"
#include "../pricing/price.hpp"

namespace firisk {

namespace {

/* Restores the curve on every exit path. A bump that survives a failed
   revaluation would silently corrupt every later price on that curve,
   and the caller has no way to detect it. */
class BumpGuard {
public:
    BumpGuard(const DiscountSource &curve) noexcept
        : curve_(curve), armed_(false) {}

    ~BumpGuard() noexcept
    {
        if (armed_)
            curve_.reset();
    }

    void arm() noexcept { armed_ = true; }

    BumpGuard(const BumpGuard &) = delete;
    BumpGuard &operator=(const BumpGuard &) = delete;

private:
    const DiscountSource &curve_;
    bool                  armed_;
};

fir_status_t priced_at_pillar_bump(const Bond           &bond,
                                   const DiscountSource &curve,
                                   double                tenor,
                                   double                bump_bp,
                                   double               *out_dirty) noexcept
{
    BumpGuard guard(curve);

    const fir_status_t brv = curve.bump(tenor, bump_bp);
    if (brv != FIR_OK)
        return brv;
    guard.arm();

    PriceComponents p{};
    const fir_status_t prv = price_from_curve(bond, curve, &p);
    if (prv != FIR_OK)
        return prv;

    *out_dirty = p.dirty;
    return FIR_OK;
}

fir_status_t priced_at_parallel_bump(const Bond           &bond,
                                     const DiscountSource &curve,
                                     double                bump_bp,
                                     double               *out_dirty) noexcept
{
    BumpGuard guard(curve);

    const fir_status_t brv = curve.bump_parallel(bump_bp);
    if (brv != FIR_OK)
        return brv;
    guard.arm();

    PriceComponents p{};
    const fir_status_t prv = price_from_curve(bond, curve, &p);
    if (prv != FIR_OK)
        return prv;

    *out_dirty = p.dirty;
    return FIR_OK;
}

} /* namespace */

fir_status_t curve_risk(const Bond           &bond,
                        const DiscountSource &curve,
                        double                bump_bp,
                        CurveRisk            *out) noexcept
{
    if (!out)
        return FIR_E_NULL_ARG;

    if (!curve.supports_parallel())
        return FIR_E_NO_BUMP_SUPPORT;

    PriceComponents base{};
    const fir_status_t rv = price_from_curve(bond, curve, &base);
    if (rv != FIR_OK)
        return rv;

    if (!(base.dirty > 0.0))
        return FIR_E_BAD_ARG;

    double up = 0.0, down = 0.0;

    fir_status_t brv = priced_at_parallel_bump(bond, curve, bump_bp, &up);
    if (brv != FIR_OK)
        return brv;

    brv = priced_at_parallel_bump(bond, curve, -bump_bp, &down);
    if (brv != FIR_OK)
        return brv;

    const double dy = bump_bp * 1e-4;

    out->effective_duration  = (down - up) / (2.0 * base.dirty * dy);
    out->effective_convexity = (up + down - 2.0 * base.dirty)
                             / (base.dirty * dy * dy);

    /* Half the up/down spread, rescaled from the bump size to 1bp. */
    out->dv01 = (down - up) / (2.0 * bump_bp);

    return FIR_OK;
}

fir_status_t key_rate_durations(Context              &ctx,
                                const Bond           &bond,
                                const DiscountSource &curve,
                                const double         *tenors,
                                double               *out_krd,
                                size_t                n,
                                double                bump_bp) noexcept
{
    if (!tenors || !out_krd)
        return FIR_E_NULL_ARG;

    if (!curve.supports_bump())
        return FIR_E_NO_BUMP_SUPPORT;

    if (n == 0)
        return FIR_OK;

    PriceComponents base{};
    const fir_status_t rv = price_from_curve(bond, curve, &base);
    if (rv != FIR_OK)
        return rv;

    if (!(base.dirty > 0.0))
        return FIR_E_BAD_ARG;

    const double dy = bump_bp * 1e-4;

    for (size_t i = 0; i < n; ++i) {
        if (!(tenors[i] > 0.0) || !std::isfinite(tenors[i]))
            return FIR_E_BAD_ARG;

        double up = 0.0, down = 0.0;

        fir_status_t brv = priced_at_pillar_bump(bond, curve, tenors[i],
                                                 bump_bp, &up);
        if (brv != FIR_OK)
            return brv;

        brv = priced_at_pillar_bump(bond, curve, tenors[i], -bump_bp, &down);
        if (brv != FIR_OK)
            return brv;

        out_krd[i] = (down - up) / (2.0 * base.dirty * dy);
    }

    /* Scratch is unused here because each bump is priced immediately.
       Touching it keeps the reservation from bond build meaningful if
       a batched variant lands later. */
    (void)ctx;

    return FIR_OK;
}

} /* namespace firisk */
