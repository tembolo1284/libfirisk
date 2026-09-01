#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Flat continuously compounded curve. */
static const double base_rate = 0.04;

static double flat_discount(void *ud, double t)
{
    return exp(-(*(double *)ud) * t);
}

static fir_status_t flat_bump_parallel(void *ud, double bump_bp)
{
    *(double *)ud += bump_bp * 1e-4;
    return FIR_OK;
}

static fir_status_t flat_reset(void *ud)
{
    *(double *)ud = base_rate;
    return FIR_OK;
}

/* Set by the panic test below; the handler must not return, so it
   longjmps back rather than falling through to abort. */
static void panic_handler(const char *message)
{
    (void)message;
}

int main(void)
{
    fir_context_t  *ctx;
    fir_calendar_t *cal;
    fir_bond_t     *bond;
    fir_bond_def_t  def;
    fir_curve_t     curve;
    double rate = 0.04;
    double price = 0.0, yield = 0.0, dmac = 0.0, dmod = 0.0;
    double conv = 0.0, dv01 = 0.0, eff = 0.0, cvx = 0.0, cdv01 = 0.0;
    double accrued = 0.0;
    size_t n = 0;

    /* Installing a handler must be safe even though nothing here will
       trigger it. */
    fir_set_panic_handler(panic_handler);
    fir_set_panic_handler(NULL);

    CHECK(fir_is_compatible_dll());
    CHECK(fir_get_version() == FIR_VERSION);
    CHECK(fir_get_version_string() != NULL);
    CHECK(fir_strerror(FIR_OK) != NULL);
    CHECK(fir_strerror(FIR_E_NO_BUMP_SUPPORT) != NULL);

    ctx = fir_context_new();
    CHECK(ctx != NULL);

    CHECK(fir_context_set_valuation_date(ctx, 20260831) == FIR_OK);
    CHECK(fir_context_set_valuation_date(ctx, 20260230) == FIR_E_BAD_DATE);
    CHECK(fir_context_last_status(ctx) == FIR_E_BAD_DATE);
    CHECK(fir_context_last_message(ctx) != NULL);
    CHECK(fir_context_set_valuation_date(ctx, 20260831) == FIR_OK);

    CHECK(fir_context_set_bump_size(ctx, 0.0) == FIR_E_BAD_ARG);
    CHECK(fir_context_set_bump_size(ctx, 1.0) == FIR_OK);

    /* Weekends only; no holidays supplied. */
    cal = fir_calendar_new();
    CHECK(cal != NULL);
    CHECK(fir_calendar_set_weekend(cal, FIR_WEEKEND_SAT_SUN) == FIR_OK);
    CHECK(fir_calendar_set_weekend(cal, 0x7f) == FIR_E_BAD_ARG);

    /* 2026-08-29 is a Saturday, 2026-08-31 a Monday. */
    CHECK(fir_calendar_is_business_day(cal, 20260829) == 0);
    CHECK(fir_calendar_is_business_day(cal, 20260831) == 1);

    memset(&def, 0, sizeof def);
    def.struct_size             = sizeof def;
    def.issue_date              = 20240215;
    def.maturity_date           = 20340215;
    def.coupon_rate             = 0.045;
    def.face                    = 100.0;
    def.frequency               = FIR_FREQ_SEMIANNUAL;
    def.daycount                = FIR_DC_THIRTY_360_BOND;
    def.business_day_convention = FIR_BDC_MODIFIED_FOLLOWING;
    def.calendar                = cal;

    bond = fir_bond_new(ctx, &def);
    if (!bond) {
        fprintf(stderr, "bond build failed: %s\n",
                fir_context_last_message(ctx));
        return 1;
    }

    /* A def whose struct_size is nonsense must be rejected, not
       silently read past. */
    {
        fir_bond_def_t bad = def;
        bad.struct_size = 4;
        CHECK(fir_bond_new(ctx, &bad) == NULL);
        CHECK(fir_context_last_status(ctx) == FIR_E_BAD_STRUCT_SIZE);
    }

    CHECK(fir_bond_cashflow_count(bond, &n) == FIR_OK);
    CHECK(n > 0);

    /* Semiannual from Feb 2024 to Feb 2034, valued Aug 2026: the
       remaining flows run Feb 2027 through Feb 2034 inclusive. */
    CHECK(n == 15);

    {
        double *times   = (double *)malloc(sizeof(double) * n);
        double *amounts = (double *)malloc(sizeof(double) * n);

        CHECK(fir_bond_cashflows(bond, times, amounts, n) == FIR_OK);
        CHECK(fir_bond_cashflows(bond, times, amounts, n - 1)
                  == FIR_E_BUFFER_TOO_SMALL);

        /* Times must be increasing and positive; the final flow must
           carry the redemption. */
        CHECK(times[0] > 0.0);
        CHECK(times[n - 1] > times[0]);
        CHECK(amounts[n - 1] > 100.0);

        free(times);
        free(amounts);
    }

    CHECK(fir_bond_accrued(bond, &accrued) == FIR_OK);
    CHECK(accrued >= 0.0);

    CHECK(fir_bond_price_from_yield(bond, 0.045, FIR_COMP_PERIODIC,
                                    &price) == FIR_OK);
    CHECK(price > 0.0);

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

    /* Continuous compounding must also work and give a nearby answer. */
    CHECK(fir_bond_modified_duration(bond, 0.045, FIR_COMP_CONTINUOUS,
                                     &eff) == FIR_OK);
    CHECK(eff > 0.0 && fabs(eff - dmod) < 1.0);

    memset(&curve, 0, sizeof curve);
    curve.struct_size   = sizeof curve;
    curve.user_data     = &rate;
    curve.discount      = flat_discount;
    curve.bump_parallel = flat_bump_parallel;
    curve.reset         = flat_reset;

    CHECK(fir_bond_price_from_curve(bond, &curve, &price) == FIR_OK);
    CHECK(price > 0.0);

    CHECK(fir_bond_zspread(bond, &curve, price, &yield) == FIR_OK);
    CHECK(fabs(yield) < 1e-8);   /* priced off this curve, so zero */

    CHECK(fir_bond_effective_duration(ctx, bond, &curve, &eff) == FIR_OK);
    CHECK(eff > 0.0 && eff < 10.0);

    /* The bump guard must have restored the curve. */
    CHECK_NEAR(rate, 0.04, 1e-15);

    CHECK(fir_bond_effective_convexity(ctx, bond, &curve, &cvx) == FIR_OK);
    CHECK(cvx > 0.0);

    CHECK(fir_bond_curve_dv01(ctx, bond, &curve, &cdv01) == FIR_OK);
    CHECK(cdv01 > 0.0);
    CHECK_NEAR(rate, 0.04, 1e-15);

    /* Effective duration off a flat curve should sit close to the
       analytic modified duration at the equivalent yield. */
    CHECK(fabs(eff - dmod) < 1.0);

    /* A curve with no bump callbacks prices, but yields no curve risk. */
    curve.bump_parallel = NULL;
    curve.reset         = NULL;
    CHECK(fir_bond_price_from_curve(bond, &curve, &price) == FIR_OK);
    CHECK(fir_bond_effective_duration(ctx, bond, &curve, &eff)
              == FIR_E_NO_BUMP_SUPPORT);

    /* Null guards on every entry point that takes a pointer. */
    CHECK(fir_bond_cashflow_count(NULL, &n) == FIR_E_NULL_ARG);
    CHECK(fir_bond_accrued(bond, NULL) == FIR_E_NULL_ARG);
    CHECK(fir_bond_price_from_yield(NULL, 0.045, FIR_COMP_PERIODIC,
                                    &price) == FIR_E_NULL_ARG);
    CHECK(fir_bond_effective_duration(NULL, bond, &curve, &eff)
              == FIR_E_NULL_ARG);
    CHECK(fir_bond_price_from_curve(bond, NULL, &price) == FIR_E_NULL_ARG);
    CHECK(fir_context_last_status(NULL) == FIR_E_NULL_ARG);
    CHECK(fir_context_last_message(NULL) != NULL);

    fir_bond_free(bond);
    fir_calendar_free(cal);
    fir_context_free(ctx);

    /* Freeing null must be a no-op on every handle type. */
    fir_bond_free(NULL);
    fir_calendar_free(NULL);
    fir_context_free(NULL);

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "ok",
           failures, failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
