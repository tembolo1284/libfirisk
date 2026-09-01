#ifndef FIRISK_CORE_PRICING_DISCOUNT_HPP_INCLUDED
#define FIRISK_CORE_PRICING_DISCOUNT_HPP_INCLUDED

#include <cmath>

#include "firisk.h"

namespace firisk {

/* Wraps the caller's fir_curve_t vtable. Holds a pointer, not a copy:
   the caller owns the struct and it must outlive any risk call using
   it, which matches the lifetime rules for every other borrowed
   pointer crossing the ABI. */
class DiscountSource {
public:
    DiscountSource() noexcept : curve_(nullptr) {}

    /* Validates struct_size and the required callback. */
    static fir_status_t validate(const fir_curve_t *curve) noexcept
    {
        if (!curve)
            return FIR_E_NULL_ARG;
        if (curve->struct_size < sizeof(fir_curve_t))
            return FIR_E_BAD_STRUCT_SIZE;
        if (!curve->discount)
            return FIR_E_NULL_ARG;
        return FIR_OK;
    }

    explicit DiscountSource(const fir_curve_t *curve) noexcept
        : curve_(curve) {}

    double discount(double t) const noexcept
    {
        return curve_->discount(curve_->user_data, t);
    }

    /* Bump and reset are optional. Effective and key-rate risk require
       them; parallel-yield risk does not. */
    bool supports_bump() const noexcept
    {
        return curve_->bump != nullptr && curve_->reset != nullptr;
    }

    fir_status_t bump(double tenor, double bump_bp) const noexcept
    {
        if (!supports_bump())
            return FIR_E_NO_BUMP_SUPPORT;
        return curve_->bump(curve_->user_data, tenor, bump_bp);
    }

    fir_status_t reset() const noexcept
    {
        if (!supports_bump())
            return FIR_E_NO_BUMP_SUPPORT;
        return curve_->reset(curve_->user_data);
    }

    /* A tenor of infinity is the agreed signal for a parallel shift of
       the whole curve. Using a sentinel rather than a separate callback
       keeps the vtable at three functions. */
    static constexpr double kParallelTenor =
        std::numeric_limits<double>::infinity();

    fir_status_t bump_parallel(double bump_bp) const noexcept
    {
        return bump(kParallelTenor, bump_bp);
    }

    const double *pillars() const noexcept { return curve_->pillars; }
    size_t pillar_count() const noexcept   { return curve_->pillar_count; }

private:
    const fir_curve_t *curve_;
};

/* Flat-yield discounting, used by the analytic risk measures where the
   whole point is a single yield rather than a curve. */
inline double discount_factor(double yield,
                              double t,
                              fir_compounding_t comp,
                              int k) noexcept
{
    switch (comp) {
    case FIR_COMP_SIMPLE:
        return 1.0 / (1.0 + yield * t);
    case FIR_COMP_CONTINUOUS:
        return std::exp(-yield * t);
    case FIR_COMP_PERIODIC:
    default:
        return std::pow(1.0 + yield / static_cast<double>(k),
                        -static_cast<double>(k) * t);
    }
}

} /* namespace firisk */

#endif /* FIRISK_CORE_PRICING_DISCOUNT_HPP_INCLUDED */
