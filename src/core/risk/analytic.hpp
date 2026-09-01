#ifndef FIRISK_CORE_RISK_ANALYTIC_HPP_INCLUDED
#define FIRISK_CORE_RISK_ANALYTIC_HPP_INCLUDED

#include "firisk.h"
#include "../instrument/bond.hpp"
#include "../pricing/price.hpp"

namespace firisk {

/* All yield-based measures in one traversal. Callers wanting several
   should use this and read the fields rather than making three calls. */
struct AnalyticRisk {
    double macaulay_duration;
    double modified_duration;
    double convexity;
    double dv01;
};

fir_status_t analytic_risk(const Bond       &bond,
                           double            yield,
                           fir_compounding_t comp,
                           AnalyticRisk     *out) noexcept;

} /* namespace firisk */

#endif /* FIRISK_CORE_RISK_ANALYTIC_HPP_INCLUDED */
