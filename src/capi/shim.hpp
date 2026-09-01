#ifndef FIRISK_CAPI_SHIM_HPP_INCLUDED
#define FIRISK_CAPI_SHIM_HPP_INCLUDED

#include "firisk.h"
#include "../core/context.hpp"
#include "../core/error.hpp"

/* The opaque handles in firisk.h are declared but never defined, so
   reinterpret_cast between them and the core classes is the whole of
   the mapping. Nothing else in the shim may cast handles directly. */
#define AS_TYPE(Type, Obj)  reinterpret_cast<Type *>(Obj)
#define AS_CTYPE(Type, Obj) reinterpret_cast<const Type *>(Obj)

namespace firisk {

inline Context *as_context(fir_context_t *ctx) noexcept
{
    return AS_TYPE(Context, ctx);
}

inline const Context *as_context(const fir_context_t *ctx) noexcept
{
    return AS_CTYPE(Context, ctx);
}

inline fir_context_t *as_handle(Context *ctx) noexcept
{
    return AS_TYPE(fir_context_t, ctx);
}

} /* namespace firisk */

/* Every exported call that can fail starts by clearing the previous
   error, so fir_context_last_message never reports a stale one from
   two calls ago. Null ctx is tolerated; the caller gets the bare code. */
#define FIR_ENTRY(ctx_handle, ctx_var)                                        \
    ::firisk::Context *ctx_var = ::firisk::as_context(ctx_handle);            \
    if (ctx_var)                                                              \
        ctx_var->error().clear()

/* Guards for calls that have no context to record the failure on. */
#define FIR_RETURN_IF_NULL(ptr, status)                                       \
    do {                                                                      \
        if (!(ptr))                                                           \
            return (status);                                                  \
    } while (0)

/* Validates a caller-allocated versioned POD. `minimum` is the size of
   the struct as it stood in the oldest ABI we still accept; growing the
   struct means bumping `current` only. */
#define FIR_CHECK_STRUCT(ctx, def, Type, minimum)                             \
    do {                                                                      \
        FIR_REQUIRE_PTR(ctx, (def), #def);                                    \
        if (!::firisk::struct_size_ok((def)->struct_size,                     \
                                      (minimum), sizeof(Type)))               \
            return (ctx) ? (ctx)->error().set(                                \
                       FIR_E_BAD_STRUCT_SIZE,                                 \
                       #Type ".struct_size is %zu, expected between %zu "     \
                       "and %zu", (def)->struct_size,                         \
                       static_cast<size_t>(minimum), sizeof(Type))            \
                   : FIR_E_BAD_STRUCT_SIZE;                                   \
    } while (0)

/* Out-parameters are written only on success, so a caller that ignores
   the status code at least sees its own initial value rather than junk. */
#define FIR_REQUIRE_OUT(ctx, ptr)                                             \
    FIR_REQUIRE_PTR(ctx, (ptr), #ptr)

#endif /* FIRISK_CAPI_SHIM_HPP_INCLUDED */
