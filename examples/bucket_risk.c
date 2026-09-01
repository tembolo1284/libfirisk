#include <stdio.h>
#include <string.h>
#include <math.h>

#include "firisk.h"

/* Piecewise-flat zero curve with per-pillar bumps, enough to exercise
   key-rate risk without pulling in a real curve library. */
#define NPILLAR 6

typedef struct {
    double tenors[NPILLAR];
    double zeros[NPILLAR];
    double bumps[NPILLAR];
} simple_curve_t;

static double zero_at(const simple_curve_t *c, double t)
{
    int i;
    for (i = 0; i < NPILLAR; ++i) {
        if (t <= c->tenors[i])
            return c->zeros[i] + c->bumps[i];
    }
    return c->zeros[NPILLAR - 1] + c->bumps[NPILLAR - 1];
}

static double curve_discount(void *ud, double t)
{
    return exp(-zero_at((const simple_curve_t *)ud, t) * t);
}

static fir_status_t curve_bump(void *ud, double tenor, double bump_bp)
{
    simple_curve_t *c = (simple_curve_t *)ud;
    int i;
    for (i = 0; i < NPILLAR; ++i) {
        if (fabs(c->tenors[i] - tenor) < 1e-9) {
            c->bumps[i] += bump_bp * 1e-4;
            return FIR_OK;
        }
    }
    return FIR_E_BAD_ARG;
}

static fir_status_t curve_reset(void *ud)
{
    memset(((simple_curve_t *)ud)->bumps, 0, sizeof(double) * NPILLAR);
    return FIR_OK;
}

int main(void)
{
    static const double tenors[NPILLAR] = {1.0, 2.0, 3.0, 5.0, 7.0, 10.0};

    fir_context_t *ctx = fir_context_new();
    fir_bond_def_t def;
    fir_bond_t    *bond;
    fir_curve_t    handle;
    simple_curve_t curve;
    double krd[NPILLAR];
    double total = 0.0;
    int i;

    memcpy(curve.tenors, tenors, sizeof tenors);
    for (i = 0; i < NPILLAR; ++i) {
        curve.zeros[i] = 0.038 + 0.002 * (double)i / (double)NPILLAR;
        curve.bumps[i] = 0.0;
    }

    fir_context_set_valuation_date(ctx, 20260831);

    memset(&def, 0, sizeof def);
    def.struct_size   = sizeof def;
    def.issue_date    = 20240215;
    def.maturity_date = 20360215;
    def.coupon_rate   = 0.045;
    def.face          = 100.0;
    def.frequency     = FIR_FREQ_SEMIANNUAL;
    def.daycount      = FIR_DC_THIRTY_360_BOND;

    bond = fir_bond_new(ctx, &def);
    if (!bond) {
        fprintf(stderr, "%s\n", fir_context_last_message(ctx));
        return 1;
    }

    memset(&handle, 0, sizeof handle);
    handle.struct_size  = sizeof handle;
    handle.user_data    = &curve;
    handle.discount     = curve_discount;
    handle.bump         = curve_bump;
    handle.reset        = curve_reset;
    handle.pillars      = tenors;
    handle.pillar_count = NPILLAR;

    if (fir_bond_key_rate_durations(bond, &handle, tenors, krd,
                                    NPILLAR) != FIR_OK) {
        fprintf(stderr, "key rate durations failed\n");
        return 1;
    }

    for (i = 0; i < NPILLAR; ++i) {
        printf("%5.1fy  %10.6f\n", tenors[i], krd[i]);
        total += krd[i];
    }

    /* Key rate durations should sum to roughly the effective duration.
       They will not match exactly: this curve's bumps are piecewise
       flat with no interpolation decay between pillars. */
    printf("sum    %10.6f\n", total);

    fir_bond_free(bond);
    fir_context_free(ctx);
    return 0;
}
