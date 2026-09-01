#include <cmath>

#include "solve.hpp"

namespace firisk {

namespace {

inline bool opposite_signs(double a, double b) noexcept
{
    return (a > 0.0 && b < 0.0) || (a < 0.0 && b > 0.0);
}

} /* namespace */

fir_status_t bracket_root(ScalarFn fn,
                          double   guess,
                          double   step,
                          double   lo,
                          double   hi,
                          double  *out_a,
                          double  *out_b) noexcept
{
    if (!out_a || !out_b || !(step > 0.0) || !(hi > lo))
        return FIR_E_BAD_ARG;

    double a = guess;
    double b = guess;

    double fa = fn.eval(fn.user_data, a);
    if (!std::isfinite(fa))
        return FIR_E_NO_CONVERGENCE;

    if (fa == 0.0) {
        *out_a = *out_b = a;
        return FIR_OK;
    }

    double fb = fa;

    /* Geometric expansion: a handful of iterations covers the whole
       plausible yield range without a fine linear scan. */
    for (int i = 0; i < 64; ++i) {
        if (a > lo) {
            a = a - step < lo ? lo : a - step;
            fa = fn.eval(fn.user_data, a);
            if (std::isfinite(fa) && opposite_signs(fa, fb)) {
                *out_a = a;
                *out_b = b;
                return FIR_OK;
            }
        }

        if (b < hi) {
            b = b + step > hi ? hi : b + step;
            fb = fn.eval(fn.user_data, b);
            if (std::isfinite(fb) && opposite_signs(fa, fb)) {
                *out_a = a;
                *out_b = b;
                return FIR_OK;
            }
        }

        if (a <= lo && b >= hi)
            break;

        step *= 1.6;
    }

    return FIR_E_NO_CONVERGENCE;
}

fir_status_t solve_newton(ScalarFnD           fn,
                          double              guess,
                          double              lo,
                          double              hi,
                          const SolverConfig &cfg,
                          double             *out_root) noexcept
{
    if (!out_root)
        return FIR_E_NULL_ARG;
    if (!(hi > lo) || cfg.max_iter < 1 || !(cfg.tolerance > 0.0))
        return FIR_E_BAD_ARG;

    double flo = 0.0, dummy = 0.0;
    double fhi = 0.0;
    fn.eval(fn.user_data, lo, &flo, &dummy);
    fn.eval(fn.user_data, hi, &fhi, &dummy);

    if (!std::isfinite(flo) || !std::isfinite(fhi))
        return FIR_E_NO_CONVERGENCE;

    if (flo == 0.0) { *out_root = lo; return FIR_OK; }
    if (fhi == 0.0) { *out_root = hi; return FIR_OK; }

    if (!opposite_signs(flo, fhi))
        return FIR_E_NO_CONVERGENCE;

    /* Orient so that f(a) < 0 < f(b); simplifies the bracket update. */
    double a = flo < 0.0 ? lo : hi;
    double b = flo < 0.0 ? hi : lo;

    double x = guess;
    if (x <= (a < b ? a : b) || x >= (a < b ? b : a))
        x = 0.5 * (a + b);

    for (int i = 0; i < cfg.max_iter; ++i) {
        double f = 0.0, df = 0.0;
        fn.eval(fn.user_data, x, &f, &df);

        if (!std::isfinite(f))
            return FIR_E_NO_CONVERGENCE;

        if (f < 0.0)
            a = x;
        else
            b = x;

        if (std::fabs(f) <= cfg.tolerance) {
            *out_root = x;
            return FIR_OK;
        }

        double next;
        if (std::isfinite(df) && df != 0.0) {
            next = x - f / df;

            /* Reject a step that leaves the bracket. */
            const double low  = a < b ? a : b;
            const double high = a < b ? b : a;
            if (!(next > low && next < high))
                next = 0.5 * (a + b);
        } else {
            next = 0.5 * (a + b);
        }

        if (std::fabs(next - x) <= cfg.tolerance * (1.0 + std::fabs(x))) {
            *out_root = next;
            return FIR_OK;
        }

        x = next;
    }

    return FIR_E_NO_CONVERGENCE;
}

fir_status_t solve_brent(ScalarFn            fn,
                         double              a,
                         double              b,
                         const SolverConfig &cfg,
                         double             *out_root) noexcept
{
    if (!out_root)
        return FIR_E_NULL_ARG;
    if (cfg.max_iter < 1 || !(cfg.tolerance > 0.0))
        return FIR_E_BAD_ARG;

    double fa = fn.eval(fn.user_data, a);
    double fb = fn.eval(fn.user_data, b);

    if (!std::isfinite(fa) || !std::isfinite(fb))
        return FIR_E_NO_CONVERGENCE;

    if (fa == 0.0) { *out_root = a; return FIR_OK; }
    if (fb == 0.0) { *out_root = b; return FIR_OK; }

    if (!opposite_signs(fa, fb))
        return FIR_E_NO_CONVERGENCE;

    double c = a, fc = fa, d = b - a, e = d;

    for (int i = 0; i < cfg.max_iter; ++i) {
        if (opposite_signs(fb, fc) == false) {
            c = a; fc = fa; d = b - a; e = d;
        }

        if (std::fabs(fc) < std::fabs(fb)) {
            a = b;  b = c;  c = a;
            fa = fb; fb = fc; fc = fa;
        }

        const double tol1 = 2.0 * 2.22e-16 * std::fabs(b) + 0.5 * cfg.tolerance;
        const double xm   = 0.5 * (c - b);

        if (std::fabs(xm) <= tol1 || fb == 0.0) {
            *out_root = b;
            return FIR_OK;
        }

        if (std::fabs(e) >= tol1 && std::fabs(fa) > std::fabs(fb)) {
            const double s = fb / fa;
            double p, q;

            if (a == c) {
                p = 2.0 * xm * s;
                q = 1.0 - s;
            } else {
                const double qq = fa / fc;
                const double r  = fb / fc;
                p = s * (2.0 * xm * qq * (qq - r) - (b - a) * (r - 1.0));
                q = (qq - 1.0) * (r - 1.0) * (s - 1.0);
            }

            if (p > 0.0)
                q = -q;
            p = std::fabs(p);

            const double min1 = 3.0 * xm * q - std::fabs(tol1 * q);
            const double min2 = std::fabs(e * q);

            if (2.0 * p < (min1 < min2 ? min1 : min2)) {
                e = d;
                d = p / q;
            } else {
                d = xm;
                e = d;
            }
        } else {
            d = xm;
            e = d;
        }

        a  = b;
        fa = fb;

        b += std::fabs(d) > tol1 ? d : (xm > 0.0 ? tol1 : -tol1);
        fb = fn.eval(fn.user_data, b);

        if (!std::isfinite(fb))
            return FIR_E_NO_CONVERGENCE;
    }

    return FIR_E_NO_CONVERGENCE;
}

} /* namespace firisk */
