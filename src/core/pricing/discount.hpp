#ifndef FIRISK_CORE_PRICING_DISCOUNT_HPP_INCLUDED
#define FIRISK_CORE_PRICING_DISCOUNT_HPP_INCLUDED

#include <cmath>
#include <cstddef>
#include <limits>

#include "firisk.h"
#include "../error.hpp"

namespace firisk {

/* Wraps the caller's fir_curve_t vtable. Holds a pointer, not a copy:
   the caller owns the struct and it must outlive any risk call using
   it, which matches the lifetime rules for every other borrowed
   pointer crossing the ABI. */
class DiscountSource {
public:
    DiscountSource() noexcept : curve_(nullptr) {}

    explicit DiscountSource(const fir_curve_t *curve) noexcept
        : curve_(curve) {}

    /* Oldest layout we accept: everything up to and including `bump`.
       Callers compiled against a struct that predates bump_parallel
       still work, they just get no parallel or effective risk. */
    static constexpr std::size_t kMinStructSize =
        offsetof(fir_curve_t, bump_parallel);

    static fir_status_t validate(const fir_curve_t *curve) noexcept
    {
        if (!curve)
            return FIR_E_NULL_ARG;
        if (!struct_size_ok(curve->struct_size, kMinStructSize,
                            sizeof(fir_curve_t)))
            return FIR_E_BAD_STRUCT_SIZE;
        if (!curve->discount)
            return FIR_E_NULL_ARG;
        return FIR_OK;
    }

    double discount(double t) const noexcept
    {
        return curve_->discount(curve_->user_data, t);
    }

    /* Per-pillar bumping, needed by key-rate risk. */
    bool supports_bump() const noexcept
    {
        return curve_->bump != nullptr && has_reset();
    }

    fir_status_t bump(double tenor, double bump_bp) const noexcept
    {
        if (!supports_bump())
            return FIR_E_NO_BUMP_SUPPORT;
        return curve_->bump(curve_->user_data, tenor, bump_bp);
    }

    /* Parallel shifting, needed by effective duration/convexity and
       curve DV01. Independent of supports_bump(): a caller may provide
       either, both, or neither. */
    bool supports_parallel() const noexcept
    {
        return has_field(offsetof(fir_curve_t, reset))
            && curve_->bump_parallel != nullptr
            && has_reset();
    }

    fir_status_t bump_parallel(double bump_bp) const noexcept
    {
        if (!supports_parallel())
            return FIR_E_NO_BUMP_SUPPORT;
        return curve_->bump_parallel(curve_->user_data, bump_bp);
    }

    fir_status_t reset() const noexcept
    {
        if (!has_reset())
            return FIR_E_NO_BUMP_SUPPORT;
        return curve_->reset(curve_->user_data);
    }

    const double *pillars() const noexcept
    {
        return has_field(sizeof(fir_curve_t)) ? curve_->pillars : nullptr;
    }

    std::size_t pillar_count() const noexcept
    {
        return has_field(sizeof(fir_curve_t)) ? curve_->pillar_count : 0;
    }

private:
    /* A caller compiled against an older, shorter fir_curve_t has not
       supplied the trailing members; reading them would be reading past
       the end of their allocation. */
    bool has_field(std::size_t required_size) const noexcept
    {
        return curve_->struct_size >= required_size;
    }

    bool has_reset() const noexcept
    {
        return has_field(offsetof(fir_curve_t, pillars))
            && curve_->reset != nullptr;
    }

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
