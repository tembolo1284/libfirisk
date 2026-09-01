#ifndef FIRISK_CORE_PRICING_SOLVE_HPP_INCLUDED
#define FIRISK_CORE_PRICING_SOLVE_HPP_INCLUDED

#include "firisk.h"

namespace firisk {

/* No std::function anywhere in the numerics: it may heap-allocate, and
   these run inside bump loops. */
struct ScalarFn {
    double (*eval)(void *user_data, double x);
    void   *user_data;
};

struct ScalarFnD {
    void  (*eval)(void *user_data, double x, double *out_f, double *out_df);
    void   *user_data;
};

struct SolverConfig {
    double tolerance;
    int    max_iter;
};

/* Expands outward from `guess` until the function changes sign, or the
   search leaves [lo, hi]. Returns FIR_E_NO_CONVERGENCE if no bracket
   exists in range — which for a yield solve means the price is
   unreachable, not that the solver was unlucky. */
fir_status_t bracket_root(ScalarFn            fn,
                          double              guess,
                          double              step,
                          double              lo,
                          double              hi,
                          double             *out_a,
                          double             *out_b) noexcept;

/* Newton with a maintained bracket: takes the Newton step when it lands
   inside the bracket and makes progress, bisects otherwise. Converges
   quadratically on well-behaved yields without ever escaping to a
   nonsensical root, which plain Newton will do on a deeply discounted
   long bond. */
fir_status_t solve_newton(ScalarFnD           fn,
                          double              guess,
                          double              lo,
                          double              hi,
                          const SolverConfig &cfg,
                          double             *out_root) noexcept;

/* Derivative-free fallback for functions where the analytic derivative
   is not worth writing (currently z-spread). */
fir_status_t solve_brent(ScalarFn            fn,
                         double              a,
                         double              b,
                         const SolverConfig &cfg,
                         double             *out_root) noexcept;

} /* namespace firisk */

#endif /* FIRISK_CORE_PRICING_SOLVE_HPP_INCLUDED */
