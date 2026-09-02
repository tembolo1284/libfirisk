#include <cstdio>

#include "firisk.h"
#include "../core/error.hpp"

namespace firisk {

const char *status_message(fir_status_t status) noexcept
{
    switch (status) {
    case FIR_OK:                 return "ok";
    case FIR_E_NULL_ARG:         return "null argument";
    case FIR_E_BAD_STRUCT_SIZE:  return "struct_size does not match any "
                                        "known version of this struct";
    case FIR_E_BAD_ARG:          return "argument out of range";
    case FIR_E_BAD_DATE:         return "invalid date";
    case FIR_E_BAD_SCHEDULE:     return "invalid coupon schedule";
    case FIR_E_NO_CONVERGENCE:   return "solver failed to converge";
    case FIR_E_BUFFER_TOO_SMALL: return "output buffer too small";
    case FIR_E_UNSUPPORTED:      return "unsupported convention or "
                                        "instrument feature";
    case FIR_E_NO_BUMP_SUPPORT:  return "curve does not provide bump and "
                                        "reset callbacks";
    case FIR_E_ALLOC:            return "allocation failed";
    default:                     return "unknown error";
    }
}

fir_status_t ErrorState::setv(fir_status_t status,
                              const char *fmt,
                              std::va_list ap) noexcept
{
    status_ = status;

    if (!fmt) {
        message_[0] = '\0';
        return status;
    }

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

    /* vsnprintf always null-terminates and never allocates. A truncated
       diagnostic is strictly better than failing inside the error path. */
    std::vsnprintf(message_, kErrorMessageSize, fmt, ap);

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

    return status;
}

fir_status_t ErrorState::set(fir_status_t status,
                             const char *fmt, ...) noexcept
{
    std::va_list ap;
    va_start(ap, fmt);
    fir_status_t rv = setv(status, fmt, ap);
    va_end(ap);
    return rv;
}

bool struct_size_ok(std::size_t given,
                    std::size_t minimum,
                    std::size_t current) noexcept
{
    return given >= minimum && given <= current;
}

} /* namespace firisk */

extern "C" {

const char *fir_strerror(fir_status_t status)
{
    return firisk::status_message(status);
}

} /* extern "C" */
