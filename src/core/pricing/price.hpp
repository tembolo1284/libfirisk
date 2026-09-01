#ifndef FIRISK_CORE_PRICING_PRICE_HPP_INCLUDED
#define FIRISK_CORE_PRICING_PRICE_HPP_INCLUDED

#include "firisk.h"
#include "../instrument/bond.hpp"
#include "discount.hpp"
#include "solve.hpp"

namespace firisk {

struct PriceComponents {
    double dirty;
    double clean;
    double accrued;
};

/* PV and its first two derivatives with respect to yield, plus the
   time-weighted PV, all in one pass over the cashflows. Every analytic
   risk measure is a ratio of these, so duration, convexity and DV01
   together cost one traversal rather than three. */
struct PvDerivatives {
    double pv;      /* dirty price                */
    double dpv;     /* dP/dy                      */
    double d2pv;    /* d2P/dy2                    */
    double twpv;    /* sum over t * cf * df       */
};

fir_status_t pv_derivatives(const Bond       &bond,
                            double            yield,
                            fir_compounding_t comp,
                            PvDerivatives    *out) noexcept;

fir_status_t price_from_yield(const Bond       &bond,
                              double            yield,
                              fir_compounding_t comp,
                              PriceComponents  *out) noexcept;

fir_status_t price_from_curve(const Bond           &bond,
                              const DiscountSource &curve,
                              PriceComponents      *out) noexcept;

/* Spread is applied continuously on top of the curve's discount
   factors: df(t) * exp(-z * t). Continuous keeps the spread
   independent of the bond's coupon frequency, so two bonds with
   different frequencies on the same curve get comparable numbers. */
fir_status_t price_from_curve_with_spread(const Bond           &bond,
                                          const DiscountSource &curve,
                                          double                spread,
                                          PriceComponents      *out) noexcept;

fir_status_t yield_from_price(const Bond         &bond,
                              double              clean_price,
                              fir_compounding_t   comp,
                              const SolverConfig &cfg,
                              double             *out_yield) noexcept;

fir_status_t zspread_from_price(const Bond           &bond,
                                const DiscountSource &curve,
                                double                clean_price,
                                const SolverConfig   &cfg,
                                double               *out_spread) noexcept;

} /* namespace firisk */

#endif /* FIRISK_CORE_PRICING_PRICE_HPP_INCLUDED */
