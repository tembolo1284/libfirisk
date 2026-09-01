#ifndef FIRISK_CORE_ERROR_HPP_INCLUDED
#define FIRISK_CORE_ERROR_HPP_INCLUDED

#include <cstdarg>
#include <cstddef>

#include "firisk.h"
#include "compat.hpp"

namespace firisk {

/* Long enough for a convention name plus two dates and a number.
   Truncation is fine; this is diagnostic, not a data channel. */
inline constexpr std::size_t kErrorMessageSize = 256;

/* Static default text for every fir_status_e value. Never null. */
const char *status_message(fir_status_t status) noexcept;

class ErrorState {
public:
    ErrorState() noexcept { clear(); }

    void clear() noexcept
    {
        status_     = FIR_OK;
        message_[0] = '\0';
    }

    /* Always returns `status` so call sites can `return err.set(...)`. */
    fir_status_t set(fir_status_t status, const char *fmt, ...) noexcept
        FIR_PRINTF_FORMAT(3, 4);

    fir_status_t setv(fir_status_t status,
                      const char *fmt,
                      std::va_list ap) noexcept;

    fir_status_t status() const noexcept { return status_; }

    const char *message() const noexcept
    {
        return message_[0] ? message_ : status_message(status_);
    }

private:
    fir_status_t status_;
    char         message_[kErrorMessageSize];
};

/* Validates the `struct_size` first member of a caller-allocated POD.
   Accepts anything from the smallest ABI-compatible size up to the
   current one, so an old caller against a new library still works. */
bool struct_size_ok(std::size_t given,
                    std::size_t minimum,
                    std::size_t current) noexcept;

} /* namespace firisk */

/* Guard clauses for the C shim. `ctx` may be null: then we can only
   return the code, with no message to attach it to. */
#define FIR_REQUIRE(ctx, cond, status, ...)                                   \
    do {                                                                      \
        if (!(cond)) {                                                        \
            if (ctx)                                                          \
                return (ctx)->error().set((status), __VA_ARGS__);             \
            return (status);                                                  \
        }                                                                     \
    } while (0)

#define FIR_REQUIRE_PTR(ctx, ptr, name)                                       \
    FIR_REQUIRE(ctx, (ptr) != nullptr, FIR_E_NULL_ARG,                        \
                "%s must not be null", (name))

#endif /* FIRISK_CORE_ERROR_HPP_INCLUDED */
