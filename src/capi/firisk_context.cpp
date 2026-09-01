#include "firisk.h"
#include "shim.hpp"
#include "../core/alloc.hpp"
#include "../core/containers.hpp"
#include "../core/context.hpp"

extern "C" {

fir_context_t *fir_context_new(void)
{
    /* Context uses FIR_IMPLEMENTS_ALLOCATORS, so this goes through the
       user's malloc. Allocation failure aborts rather than returning
       null, so there is no null check to write here. */
    return firisk::as_handle(new firisk::Context());
}

void fir_context_free(fir_context_t *ctx)
{
    if (!ctx)
        return;
    delete firisk::as_context(ctx);
}

fir_status_t fir_context_set_valuation_date(fir_context_t *ctx,
                                            fir_date_t date)
{
    FIR_ENTRY(ctx, c);
    FIR_RETURN_IF_NULL(c, FIR_E_NULL_ARG);

    return c->set_valuation_date(date);
}

fir_status_t fir_context_set_solver_tolerance(fir_context_t *ctx,
                                              double tol)
{
    FIR_ENTRY(ctx, c);
    FIR_RETURN_IF_NULL(c, FIR_E_NULL_ARG);

    return c->set_solver_tolerance(tol);
}

fir_status_t fir_context_set_solver_max_iter(fir_context_t *ctx,
                                             int max_iter)
{
    FIR_ENTRY(ctx, c);
    FIR_RETURN_IF_NULL(c, FIR_E_NULL_ARG);

    return c->set_solver_max_iter(max_iter);
}

fir_status_t fir_context_set_bump_size(fir_context_t *ctx, double bp)
{
    FIR_ENTRY(ctx, c);
    FIR_RETURN_IF_NULL(c, FIR_E_NULL_ARG);

    return c->set_bump_size(bp);
}

fir_status_t fir_context_last_status(const fir_context_t *ctx)
{
    if (!ctx)
        return FIR_E_NULL_ARG;

    return firisk::as_context(ctx)->error().status();
}

const char *fir_context_last_message(const fir_context_t *ctx)
{
    if (!ctx)
        return firisk::status_message(FIR_E_NULL_ARG);

    /* Points into the context's fixed buffer. Valid until the next
       call that takes this context. Callers who keep it must copy. */
    return firisk::as_context(ctx)->error().message();
}

} /* extern "C" */
