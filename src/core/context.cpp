#include "context.hpp"
#include "pricing/solve.hpp"

namespace firisk {

namespace {

/* Structural YYYYMMDD check only. Once time/date.hpp lands this
   delegates to Date::is_valid, which knows about leap years. */
bool plausible_date(fir_date_t date) noexcept
{
    if (date < 10000101 || date > 99991231)
        return false;

    int month = (date / 100) % 100;
    int day   = date % 100;

    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

} /* namespace */

Context::Context() noexcept
    : valuation_date_(0)
    , solver_tolerance_(1e-12)
    , solver_max_iter_(100)
    , bump_size_(1.0)
    , error_()
    , scratch_values_()
{
}

fir_status_t Context::set_valuation_date(fir_date_t date) noexcept
{
    if (!plausible_date(date))
        return error_.set(FIR_E_BAD_DATE,
                          "valuation date %d is not a valid YYYYMMDD date",
                          static_cast<int>(date));

    valuation_date_ = date;
    return FIR_OK;
}

fir_status_t Context::set_solver_tolerance(double tol) noexcept
{
    if (!(tol > 0.0) || tol > 1.0)
        return error_.set(FIR_E_BAD_ARG,
                          "solver tolerance must be in (0, 1], got %g", tol);

    solver_tolerance_ = tol;
    return FIR_OK;
}

fir_status_t Context::set_solver_max_iter(int max_iter) noexcept
{
    if (max_iter < 1 || max_iter > 100000)
        return error_.set(FIR_E_BAD_ARG,
                          "solver max iterations must be in [1, 100000], "
                          "got %d", max_iter);

    solver_max_iter_ = max_iter;
    return FIR_OK;
}

fir_status_t Context::set_bump_size(double bp) noexcept
{
    /* Below ~0.01bp the central difference loses more to cancellation
       in double precision than it gains in truncation error. */
    if (!(bp >= 0.01) || bp > 1000.0)
        return error_.set(FIR_E_BAD_ARG,
                          "bump size must be in [0.01, 1000] basis points, "
                          "got %g", bp);

    bump_size_ = bp;
    return FIR_OK;
}

} /* namespace firisk */
