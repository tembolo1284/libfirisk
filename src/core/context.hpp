#ifndef FIRISK_CORE_CONTEXT_HPP_INCLUDED
#define FIRISK_CORE_CONTEXT_HPP_INCLUDED

#include "firisk.h"
#include "alloc.hpp"
#include "containers.hpp"
#include "error.hpp"
#include "pricing/solve.hpp"

namespace firisk {

class Context {
public:
    Context() noexcept;

    fir_status_t set_valuation_date(fir_date_t date) noexcept;
    fir_status_t set_solver_tolerance(double tol) noexcept;
    fir_status_t set_solver_max_iter(int max_iter) noexcept;
    fir_status_t set_bump_size(double bp) noexcept;

    fir_date_t valuation_date() const noexcept   { return valuation_date_; }
    double     solver_tolerance() const noexcept { return solver_tolerance_; }
    int        solver_max_iter() const noexcept  { return solver_max_iter_; }
    double     bump_size() const noexcept        { return bump_size_; }

    /* Solver settings as the numerics layer wants them. */
    SolverConfig solver_config() const noexcept
    {
        SolverConfig cfg;
        cfg.tolerance = solver_tolerance_;
        cfg.max_iter  = solver_max_iter_;
        return cfg;
    }

    ErrorState       &error() noexcept       { return error_; }
    const ErrorState &error() const noexcept { return error_; }

    /* Reusable buffer for bumped revaluations. Reserved to the bond's
       cashflow count at build time so a batched risk sweep can fill it
       without allocating. Nothing reads it yet: curve_risk prices each
       bump as it goes, so this exists for the batched variant only. */
    Vector<double> &scratch_values() noexcept { return scratch_values_; }

private:
    FIR_IMPLEMENTS_ALLOCATORS;

    fir_date_t valuation_date_;
    double     solver_tolerance_;
    int        solver_max_iter_;
    double     bump_size_;

    ErrorState error_;

    Vector<double> scratch_values_;
};

} /* namespace firisk */

#endif /* FIRISK_CORE_CONTEXT_HPP_INCLUDED */
