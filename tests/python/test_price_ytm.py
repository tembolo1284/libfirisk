"""Pricing, yield solving, and z-spread through the C ABI."""

import math

import pytest

from testhelpers import FlatCurve, ffi, lib, new_bond


def price_at(bond, yield_, comp=None):
    comp = lib.FIR_COMP_PERIODIC if comp is None else comp
    out = ffi.new("double *")
    status = lib.fir_bond_price_from_yield(bond, yield_, comp, out)
    assert status == lib.FIR_OK
    return out[0]


def yield_at(bond, price, comp=None):
    comp = lib.FIR_COMP_PERIODIC if comp is None else comp
    out = ffi.new("double *")
    status = lib.fir_bond_yield_from_price(bond, price, comp, out)
    assert status == lib.FIR_OK
    return out[0]


class TestPriceFromYield:
    def test_par_bond_prices_at_par(self, ctx):
        # Coupon equal to yield under matching conventions gives par.
        bond = new_bond(ctx, coupon_rate=0.05, daycount=lib.FIR_DC_THIRTY_360_BOND)
        clean = price_at(bond, 0.05)

        assert clean == pytest.approx(100.0, abs=0.05)

    def test_discount_when_yield_above_coupon(self, bond):
        assert price_at(bond, 0.06) < 100.0

    def test_premium_when_yield_below_coupon(self, bond):
        assert price_at(bond, 0.03) > 100.0

    def test_price_decreases_in_yield(self, bond):
        prices = [price_at(bond, y) for y in (0.01, 0.03, 0.05, 0.07, 0.09)]
        assert all(b < a for a, b in zip(prices, prices[1:]))

    def test_zero_yield_sums_undiscounted_cashflows(self, bond):
        from testhelpers import cashflows

        _, amounts = cashflows(bond)
        accrued = ffi.new("double *")
        lib.fir_bond_accrued(bond, accrued)

        assert price_at(bond, 0.0) == pytest.approx(sum(amounts) - accrued[0])

    @pytest.mark.parametrize("comp", [
        lib.FIR_COMP_SIMPLE,
        lib.FIR_COMP_PERIODIC,
        lib.FIR_COMP_CONTINUOUS,
    ])
    def test_every_compounding_prices(self, bond, comp):
        assert price_at(bond, 0.045, comp) > 0.0

    def test_compounding_orders_as_expected(self, bond):
        # For a given quoted rate, more frequent compounding discounts
        # harder, so continuous gives the lowest price.
        simple = price_at(bond, 0.05, lib.FIR_COMP_SIMPLE)
        periodic = price_at(bond, 0.05, lib.FIR_COMP_PERIODIC)
        continuous = price_at(bond, 0.05, lib.FIR_COMP_CONTINUOUS)

        assert continuous < periodic < simple

    def test_periodic_singularity_is_rejected(self, bond):
        # Semiannual: 1 + y/2 <= 0 means y <= -2.
        out = ffi.new("double *")
        status = lib.fir_bond_price_from_yield(
            bond, -2.5, lib.FIR_COMP_PERIODIC, out)
        assert status == lib.FIR_E_BAD_ARG

    def test_zero_coupon_price(self, zero_bond):
        from testhelpers import cashflows

        times, _ = cashflows(zero_bond)
        expected = 100.0 * math.exp(-0.04 * times[0])

        clean = price_at(zero_bond, 0.04, lib.FIR_COMP_CONTINUOUS)
        assert clean == pytest.approx(expected, rel=1e-12)


class TestYieldFromPrice:
    @pytest.mark.parametrize("yield_", [0.001, 0.02, 0.045, 0.08, 0.15, 0.40])
    def test_round_trip(self, bond, yield_):
        clean = price_at(bond, yield_)
        assert yield_at(bond, clean) == pytest.approx(yield_, abs=1e-9)

    @pytest.mark.parametrize("comp", [
        lib.FIR_COMP_SIMPLE,
        lib.FIR_COMP_PERIODIC,
        lib.FIR_COMP_CONTINUOUS,
    ])
    def test_round_trip_every_compounding(self, bond, comp):
        clean = price_at(bond, 0.05, comp)
        assert yield_at(bond, clean, comp) == pytest.approx(0.05, abs=1e-9)

    def test_negative_yield_solves(self, bond):
        clean = price_at(bond, -0.005)
        assert yield_at(bond, clean) == pytest.approx(-0.005, abs=1e-9)

    def test_zero_coupon_round_trip(self, zero_bond):
        clean = price_at(zero_bond, 0.037)
        assert yield_at(zero_bond, clean) == pytest.approx(0.037, abs=1e-9)

    def test_unreachable_price_fails_cleanly(self, bond):
        # No yield in the solver's range produces a negative price.
        out = ffi.new("double *")
        status = lib.fir_bond_yield_from_price(
            bond, -50.0, lib.FIR_COMP_PERIODIC, out)
        assert status == lib.FIR_E_NO_CONVERGENCE


class TestPriceFromCurve:
    def test_flat_curve_matches_continuous_yield(self, bond, flat_curve):
        out = ffi.new("double *")
        status = lib.fir_bond_price_from_curve(bond, flat_curve.handle, out)
        assert status == lib.FIR_OK

        # The curve is exp(-0.04 t), which is exactly continuous
        # discounting at 4%.
        assert out[0] == pytest.approx(
            price_at(bond, 0.04, lib.FIR_COMP_CONTINUOUS), rel=1e-12)

    def test_curve_without_bumps_still_prices(self, bond, flat_curve):
        flat_curve.drop_bump_support()

        out = ffi.new("double *")
        assert lib.fir_bond_price_from_curve(bond, flat_curve.handle, out) == lib.FIR_OK
        assert out[0] > 0.0

    def test_null_curve_is_rejected(self, bond):
        out = ffi.new("double *")
        status = lib.fir_bond_price_from_curve(bond, ffi.NULL, out)
        assert status == lib.FIR_E_NULL_ARG

    @pytest.mark.abi
    def test_bad_struct_size_is_rejected(self, bond, flat_curve):
        flat_curve.handle.struct_size = 4

        out = ffi.new("double *")
        status = lib.fir_bond_price_from_curve(bond, flat_curve.handle, out)
        assert status == lib.FIR_E_BAD_STRUCT_SIZE

    def test_higher_curve_gives_lower_price(self, bond):
        low = FlatCurve(rate=0.02)
        high = FlatCurve(rate=0.06)

        a = ffi.new("double *")
        b = ffi.new("double *")
        lib.fir_bond_price_from_curve(bond, low.handle, a)
        lib.fir_bond_price_from_curve(bond, high.handle, b)

        assert b[0] < a[0]


class TestZSpread:
    def test_own_curve_gives_zero_spread(self, bond, flat_curve):
        priced = ffi.new("double *")
        lib.fir_bond_price_from_curve(bond, flat_curve.handle, priced)

        out = ffi.new("double *")
        status = lib.fir_bond_zspread(bond, flat_curve.handle, priced[0], out)
        assert status == lib.FIR_OK
        assert out[0] == pytest.approx(0.0, abs=1e-10)

    def test_cheaper_price_gives_positive_spread(self, bond, flat_curve):
        priced = ffi.new("double *")
        lib.fir_bond_price_from_curve(bond, flat_curve.handle, priced)

        out = ffi.new("double *")
        lib.fir_bond_zspread(bond, flat_curve.handle, priced[0] - 5.0, out)
        assert out[0] > 0.0

    def test_richer_price_gives_negative_spread(self, bond, flat_curve):
        priced = ffi.new("double *")
        lib.fir_bond_price_from_curve(bond, flat_curve.handle, priced)

        out = ffi.new("double *")
        lib.fir_bond_zspread(bond, flat_curve.handle, priced[0] + 5.0, out)
        assert out[0] < 0.0

    def test_spread_round_trips_through_a_shifted_curve(self, bond):
        base = FlatCurve(rate=0.04)
        shifted = FlatCurve(rate=0.05)

        # Price off the 5% curve, then ask what spread over the 4%
        # curve reproduces it. Continuous spread over a continuous flat
        # curve should recover exactly 100bp.
        target = ffi.new("double *")
        lib.fir_bond_price_from_curve(bond, shifted.handle, target)

        out = ffi.new("double *")
        lib.fir_bond_zspread(bond, base.handle, target[0], out)
        assert out[0] == pytest.approx(0.01, abs=1e-8)
