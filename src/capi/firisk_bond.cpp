#include "firisk.h"
#include "shim.hpp"
#include "../core/instrument/bond.hpp"
#include "../core/time/calendar.hpp"

extern "C" {

fir_bond_t *fir_bond_new(fir_context_t *ctx, const fir_bond_def_t *def)
{
    firisk::Context *c = firisk::as_context(ctx);
    if (!c)
        return nullptr;

    c->error().clear();

    if (!def) {
        c->error().set(FIR_E_NULL_ARG, "def must not be null");
        return nullptr;
    }

    /* Minimum accepted layout is the struct as it stood before the
       calendar field was appended. */
    const size_t minimum = offsetof(fir_bond_def_t, calendar);

    if (!firisk::struct_size_ok(def->struct_size, minimum,
                                sizeof(fir_bond_def_t))) {
        c->error().set(FIR_E_BAD_STRUCT_SIZE,
                       "fir_bond_def_t.struct_size is %zu, expected "
                       "between %zu and %zu",
                       def->struct_size, minimum, sizeof(fir_bond_def_t));
        return nullptr;
    }

    /* Weekend-only default, on the stack: Bond keeps adjusted dates,
       not the calendar, so this need not outlive the call. */
    const firisk::Calendar  fallback;
    const firisk::Calendar *cal = &fallback;

    if (def->struct_size >= sizeof(fir_bond_def_t) && def->calendar)
        cal = AS_CTYPE(firisk::Calendar, def->calendar);

    firisk::UniquePtr<firisk::Bond> bond = firisk::make<firisk::Bond>();

    if (bond->build(*c, *def, *cal) != FIR_OK)
        return nullptr;

    return AS_TYPE(fir_bond_t, bond.release());
}

void fir_bond_free(fir_bond_t *bond)
{
    if (!bond)
        return;
    delete AS_TYPE(firisk::Bond, bond);
}

fir_status_t fir_bond_cashflow_count(const fir_bond_t *bond, size_t *out_count)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    FIR_RETURN_IF_NULL(out_count, FIR_E_NULL_ARG);

    *out_count = AS_CTYPE(firisk::Bond, bond)->cashflow_count();
    return FIR_OK;
}

fir_status_t fir_bond_cashflows(const fir_bond_t *bond,
                                double           *out_times,
                                double           *out_amounts,
                                size_t            n)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);

    const firisk::Bond *b = AS_CTYPE(firisk::Bond, bond);
    const size_t count = b->cashflow_count();

    if (n < count)
        return FIR_E_BUFFER_TOO_SMALL;

    for (size_t i = 0; i < count; ++i) {
        if (out_times)
            out_times[i] = b->cashflows()[i].time;
        if (out_amounts)
            out_amounts[i] = b->cashflows()[i].amount;
    }

    return FIR_OK;
}

fir_status_t fir_bond_accrued(const fir_bond_t *bond, double *out_accrued)
{
    FIR_RETURN_IF_NULL(bond, FIR_E_NULL_ARG);
    return AS_CTYPE(firisk::Bond, bond)->accrued(out_accrued);
}

} /* extern "C" */
