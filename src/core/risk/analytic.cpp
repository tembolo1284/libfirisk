#include <cmath>

#include "analytic.hpp"

namespace firisk {

fir_status_t analytic_risk(const Bond       &bond,
                           double            yield,
                           fir_compounding_t comp,
                           AnalyticRisk     *out) noexcept
{
    if (!out)
        return FIR_E_NULL_ARG;

    PvDerivatives d{};
    const fir_status_t rv = pv_derivatives(bond, yield, comp, &d);
    if (rv != FIR_OK)
        return rv;

    /* Every measure divides by PV. A zero or negative dirty price is
       not a bond we can produce sensible risk for. */
    if (!(d.pv > 0.0))
        return FIR_E_BAD_ARG;

    /* Macaulay is the PV-weighted mean time to cashflow — the
       whiteboard's sum(t * CF_t / (1+y)^t) / P, computed from the
       accumulated time-weighted PV. */
    out->macaulay_duration = d.twpv / d.pv;

    /* Modified duration is defined directly as -(1/P) dP/dy. For
       periodic compounding this equals D_mac / (1 + y/k) identically;
       deriving it from dP/dy instead means continuous and simple
       compounding fall out correctly without a special case. */
    out->modified_duration = -d.dpv / d.pv;

    out->convexity = d.d2pv / d.pv;

    /* Same units as the dirty price: cashflow amounts are scaled by
       `face`, so a face of 100 gives DV01 per 100 of notional. */
    out->dv01 = -d.dpv * 1e-4;

    return FIR_OK;
}

} /* namespace firisk */
