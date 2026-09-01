#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "firisk.h"

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
            ++failures;                                                    \
        }                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabs((a) - (b)) < (tol))

/* Flat 4% continuously compounded curve. */
static double flat_discount(void *ud, double t)
{
    return exp(-(*(double *)ud) * t);
}

static fir_status_t flat_bump_parallel(void *ud, double bump_bp)
{
    *(double *)ud += bump_bp * 1e-4;
    return FIR_OK;
}

static double base_rate = 0.04;

static fir_status_t flat_reset(void *ud)
{
    *(double *)ud = base_rate;
    return FIR_OK;
}

int main(void)
{
    fir_context_t *ctx;
    fir_bond_t    *bond;
    fir_bond_def_t def;
    fir_curve_t    curve;
    double rate = 0.04;
    double price = 0.0, yield = 0.0, dmac = 0.0, dmod = 0.0;
    double conv = 0.0, dv01 = 0.0, eff = 0.0;
    size_t n = 0;

    CHECK(fir_is_compatible_dll());
    CHECK(fir_get_version() == FIR_VERSION);

    ctx = fir_context_new();
    CHECK(ctx != NULL);

    CHECK(fir_context_set_valuation_date(ctx, 20260831) == FIR_OK);
    CHECK(fir_context_set_valuation_date(ctx, 20260230) == FIR_E_BAD_DATE);
    CHECK(fir_context_set_valuation_date(ctx, 20260831) == FIR_OK);

    memset(&def, 0, sizeof def);
    def.struct_size             = sizeof def;
    def.issue_date              = 20240215;
    def.maturity_date           = 20340215;
    def.coupon_rate             = 0.045;
    def.face                    = 100.0;
    def.frequency               = FIR_FREQ_SEMIANNUAL;
    def.daycount                = FIR_DC_THIRTY_360_BOND;
    def.business_day_convention = FIR_BDC_MODIFIED_FOLLOWING;

    bond = fir_bond_new(ctx, &def);
    if (!bond) {
        fprintf(stderr, "bond build failed: %s\n",
                fir_context_last_message(ctx));
        return 1;
    }

    CHECK(fir_bond_cashflow_count(bond, &n) == FIR_OK);
    CHECK(n == 15);   /* Feb 2027 .. Feb 2034, semiannual */

    CHECK(fir_bond_price_from_yield(bond, 0.045, FIR_COMP_PERIODIC,
                                    &price) == FIR_OK);

    /* Round trip: solving the yield back from that price must return
       the yield we priced at. */
    CHECK(fir_bond_yield_from_price(bond, price, FIR_COMP_PERIODIC,
                                    &yield) == FIR_OK);
    CHECK_NEAR(yield, 0.045, 1e-8);

    CHECK(fir_bond_macaulay_duration(bond, 0.045, FIR_COMP_PERIODIC,
                                     &dmac) == FIR_OK);
    CHECK(fir_bond_modified_duration(bond, 0.045, FIR_COMP_PERIODIC,
                                     &dmod) == FIR_OK);

    /* The whiteboard identity: D_mod = D_mac / (1 + y/k). */
    CHECK_NEAR(dmod, dmac / (1.0 + 0.045 / 2.0), 1e-10);
    CHECK(dmac > 0.0 && dmac < 10.0);

    CHECK(fir_bond_convexity(bond, 0.045, FIR_COMP_PERIODIC,
                             &conv) == FIR_OK);
    CHECK(conv > 0.0);

    CHECK(fir_bond_dv01(bond, 0.045, FIR_COMP_PERIODIC, &dv01) == FIR_OK);
    CHECK(dv01 > 0.0);

    memset(&curve, 0, sizeof curve);
    curve.struct_size   = sizeof curve;
    curve.user_data     = &rate;
    curve.discount      = flat_discount;
    curve.bump_parallel = flat_bump_parallel;
    curve.reset         = flat_reset;

    CHECK(fir_bond_price_from_curve(bond, &curve, &price) == FIR_OK);
    CHECK(price > 0.0);

    CHECK(fir_bond_effective_duration(bond, &curve, &eff) == FIR_OK);
    CHECK(eff > 0.0 && eff < 10.0);

    /* The bump guard must have restored the curve. */
    CHECK_NEAR(rate, 0.04, 1e-15);

    /* A curve with no bump callbacks prices, but yields no curve risk. */
    curve.bump_parallel = NULL;
    curve.reset         = NULL;
    CHECK(fir_bond_effective_duration(bond, &curve, &eff)
              == FIR_E_NO_BUMP_SUPPORT);

    CHECK(fir_bond_accrued(bond, &price) == FIR_OK);

    fir_bond_free(bond);
    fir_context_free(ctx);

    fir_bond_free(NULL);
    fir_context_free(NULL);

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "ok",
           failures, failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
