#include <stdio.h>
#include <string.h>

#include "firisk.h"

int main(void)
{
    fir_context_t *ctx = fir_context_new();
    fir_bond_def_t def;
    fir_bond_t    *bond;
    double price = 0.0, yield = 0.0, dmac = 0.0, dmod = 0.0, dv01 = 0.0;

    fir_context_set_valuation_date(ctx, 20260831);

    memset(&def, 0, sizeof def);
    def.struct_size   = sizeof def;
    def.issue_date    = 20240215;
    def.maturity_date = 20340215;
    def.coupon_rate   = 0.045;
    def.face          = 100.0;
    def.frequency     = FIR_FREQ_SEMIANNUAL;
    def.daycount      = FIR_DC_THIRTY_360_BOND;

    bond = fir_bond_new(ctx, &def);
    if (!bond) {
        fprintf(stderr, "%s\n", fir_context_last_message(ctx));
        fir_context_free(ctx);
        return 1;
    }

    fir_bond_price_from_yield(bond, 0.05, FIR_COMP_PERIODIC, &price);
    fir_bond_yield_from_price(bond, price, FIR_COMP_PERIODIC, &yield);
    fir_bond_macaulay_duration(bond, yield, FIR_COMP_PERIODIC, &dmac);
    fir_bond_modified_duration(bond, yield, FIR_COMP_PERIODIC, &dmod);
    fir_bond_dv01(bond, yield, FIR_COMP_PERIODIC, &dv01);

    printf("clean price   %10.6f\n", price);
    printf("yield         %10.6f\n", yield);
    printf("D_mac         %10.6f\n", dmac);
    printf("D_mod         %10.6f\n", dmod);
    printf("DV01          %10.6f\n", dv01);

    fir_bond_free(bond);
    fir_context_free(ctx);
    return 0;
}
