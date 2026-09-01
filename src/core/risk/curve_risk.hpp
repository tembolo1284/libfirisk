#ifndef FIRISK_CORE_RISK_CURVE_RISK_HPP_INCLUDED
#define FIRISK_CORE_RISK_CURVE_RISK_HPP_INCLUDED

#include "firisk.h"
#include "../containers.hpp"
#include "../instrument/bond.hpp"
#include "../pricing/discount.hpp"

namespace firisk {

class Context;

struct CurveRisk {
    double effective_duration;
    double effective_convexity;
    double dv01;
};

/* Central-difference parallel shift. Requires bump_parallel and reset
   on the curve; returns FIR_E_NO_BUMP_SUPPORT otherwise. */
fir_status_t curve_risk(const Bond           &bond,
                        const DiscountSource &curve,
                        double                bump_bp,
                        CurveRisk            *out) noexcept;

/* One central-difference sensitivity per tenor, written to out_krd.
   Uses the context's scratch buffer, so it allocates only if the
   bond was built on a different context. */
fir_status_t key_rate_durations(Context              &ctx,
                                const Bond           &bond,
                                const DiscountSource &curve,
                                const double         *tenors,
                                double               *out_krd,
                                size_t                n,
                                double                bump_bp) noexcept;

} /* namespace firisk */

#endif /* FIRISK_CORE_RISK_CURVE_RISK_HPP_INCLUDED */
