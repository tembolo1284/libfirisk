#ifndef FIRISK_CORE_CONTEXT_HPP_INCLUDED
#define FIRISK_CORE_CONTEXT_HPP_INCLUDED

#include "firisk.h"
#include "alloc.hpp"
#include "containers.hpp"
#include "error.hpp"

namespace firisk {

class Context {
public:
    Context() noexcept;

    fir_status_t set_valuation_date(fir_date_t date) noexcept;
    fir_status_t set_solver_tolerance(double tol) noexcept;
    fir_status_t set_solver_max_iter(int max_iter) noexcept;
    fir_status_t set_bump_size(double bp) noexcept;

    fir_date_t valuation_date() const noexcept  { return valuation_date_; }
    double     solver_tolerance() const noexcept { return solver_tolerance_; }
    int        solver_max_iter() const noexcept  { return solver_max_iter_; }
    double     bump_size() const noexcept        { return bump_size_; }

    ErrorState       &error() noexcept       { return error_; }
    const ErrorState &error() const noexcept { return error_; }

    /* Reusable buffers for cashflow times/amounts and bumped prices.
       Sized once per bond, then reused across every risk call, so a
       key-rate sweep over 12 pillars does no allocation at all. */
    Vector<double> &scratch_times()   noexcept { return scratch_times_; }
    Vector<double> &scratch_amounts() noexcept { return scratch_amounts_; }
    Vector<double> &scratch_values()  noexcept { return scratch_values_; }

private:
    FIR_IMPLEMENTS_ALLOCATORS;

    fir_date_t valuation_date_;
    double     solver_tolerance_;
    int        solver_max_iter_;
    double     bump_size_;

    ErrorState error_;

    Vector<double> scratch_times_;
    Vector<double> scratch_amounts_;
    Vector<double> scratch_values_;
};

} /* namespace firisk */

#endif /* FIRISK_CORE_CONTEXT_HPP_INCLUDED */
