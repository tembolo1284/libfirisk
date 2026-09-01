"""Analytic yield-based risk measures through the C ABI."""

import pytest

from testhelpers import cashflows, ffi, lib, new_bond


def risk(bond, fn, yield_=0.045, comp=None):
    comp = lib.FIR_COMP_PERIODIC if comp is None else comp
    out = ffi.new("double *")
    assert fn(bond, yield_, comp, out) == lib.FIR_OK
    return out[0]


def macaulay(bond, yield_=0.045, comp=None):
    return risk(bond, lib.fir_bond_macaulay_duration, yield_, comp)


def modified(bond, yield_=0.045, comp=None):
    return risk(bond, lib.fir_bond_modified_duration, yield_, comp)


def convexity(bond, yield_=0.045, comp=None):
    return risk(bond, lib.fir_bond_convexity, yield_, comp)


def dv01(bond, yield_=0.045, comp=None):
    return risk(bond, lib.fir_bond_dv01, yield_, comp)


def price(bond, yield_, comp=None):
    comp = lib.FIR_COMP_PERIODIC if comp is None else comp
    out = ffi.new("double *")
    lib.fir_bond_price_from_yield(bond, yield_, comp, out)
    return out[0]


class TestMacaulay:
    def test_positive_and_below_maturity(self, bond):
        times, _ = cashflows(bond)
        assert 0.0 < macaulay(bond) < times[-1]

    def test_zero_coupon_duration_equals_time_to_maturity(self, zero_bond):
        times, _ = cashflows(zero_bond)

        # With one cashflow, the PV-weighted mean time is that time.
        assert macaulay(zero_bond, 0.04) == pytest.approx(times[0], rel=1e-12)

    def test_coupon_bond_duration_below_maturity(self, bond):
        times, _ = cashflows(bond)
        assert macaulay(bond) < times[-1] - 1.0

    def test_duration_falls_as_yield_rises(self, bond):
        assert macaulay(bond, 0.08) < macaulay(bond, 0.02)

    def test_higher_coupon_shortens_duration(self, ctx):
        low = new_bond(ctx, coupon_rate=0.01)
        high = new_bond(ctx, coupon_rate=0.09)

        assert macaulay(high) < macaulay(low)


class TestModified:
    def test_whiteboard_identity(self, bond):
        # D_mod = D_mac / (1 + y/k), k = 2 for semiannual.
        assert modified(bond, 0.045) == pytest.approx(
            macaulay(bond, 0.045) / (1.0 + 0.045 / 2.0), rel=1e-12)

    def test_identity_holds_for_annual(self, ctx):
        annual = new_bond(ctx, frequency=lib.FIR_FREQ_ANNUAL)
        assert modified(annual, 0.05) == pytest.approx(
            macaulay(annual, 0.05) / (1.0 + 0.05), rel=1e-12)

    def test_identity_holds_for_quarterly(self, ctx):
        quarterly = new_bond(ctx, frequency=lib.FIR_FREQ_QUARTERLY)
        assert modified(quarterly, 0.05) == pytest.approx(
            macaulay(quarterly, 0.05) / (1.0 + 0.05 / 4.0), rel=1e-12)

    def test_continuous_modified_equals_macaulay(self, bond):
        # Under continuous compounding there is no 1/(1+y/k) factor.
        comp = lib.FIR_COMP_CONTINUOUS
        assert modified(bond, 0.045, comp) == pytest.approx(
            macaulay(bond, 0.045, comp), rel=1e-12)

    def test_matches_a_one_basis_point_reprice(self, bond):
        # A one-sided difference differs from the analytic derivative by
        # the convexity term; a central difference is far closer.
        up = price(bond, 0.045 + 1e-4)
        down = price(bond, 0.045 - 1e-4)

        assert dv01(bond) == pytest.approx((down - up) / 2.0, rel=1e-6)

class TestConvexity:
    def test_positive_for_a_vanilla_bond(self, bond):
        assert convexity(bond) > 0.0

    def test_zero_coupon_convexity_exceeds_coupon_bond(self, ctx, zero_bond):
        coupon = new_bond(ctx)
        # Longer effective maturity means more curvature.
        assert convexity(zero_bond, 0.04) > convexity(coupon, 0.04)

    def test_improves_the_price_prediction(self, bond):
        # Second-order: dP/P = -D_mod dy + C dy^2 / 2.
        base = price(bond, 0.045)
        dy = 0.01
        bumped = price(bond, 0.045 + dy)

        first = -modified(bond, 0.045) * dy * base
        second = 0.5 * convexity(bond, 0.045) * dy * dy * base

        actual = bumped - base
        assert abs(actual - (first + second)) < abs(actual - first)


class TestDv01:
    def test_positive_for_a_vanilla_bond(self, bond):
        assert dv01(bond) > 0.0

    def test_matches_a_one_basis_point_reprice(self, bond):
        # A one-sided difference differs from the analytic derivative by
        # the convexity term; a central difference is far closer.
        up = price(bond, 0.045 + 1e-4)
        down = price(bond, 0.045 - 1e-4)

        assert dv01(bond) == pytest.approx((down - up) / 2.0, rel=1e-6)

    def test_consistent_with_modified_duration(self, bond):
        # DV01 = D_mod * P_dirty * 1bp.
        accrued = ffi.new("double *")
        lib.fir_bond_accrued(bond, accrued)
        dirty = price(bond, 0.045) + accrued[0]

        assert dv01(bond) == pytest.approx(
            modified(bond, 0.045) * dirty * 1e-4, rel=1e-12)

    def test_longer_maturity_has_larger_dv01(self, ctx):
        short = new_bond(ctx, maturity_date=20290215)
        long_ = new_bond(ctx, maturity_date=20460215)

        assert dv01(long_) > dv01(short)


class TestGuards:
    def test_null_bond(self):
        out = ffi.new("double *")
        status = lib.fir_bond_macaulay_duration(
            ffi.NULL, 0.045, lib.FIR_COMP_PERIODIC, out)
        assert status == lib.FIR_E_NULL_ARG

    def test_null_output(self, bond):
        status = lib.fir_bond_modified_duration(
            bond, 0.045, lib.FIR_COMP_PERIODIC, ffi.NULL)
        assert status == lib.FIR_E_NULL_ARG

    def test_yield_past_the_singularity(self, bond):
        out = ffi.new("double *")
        status = lib.fir_bond_convexity(
            bond, -3.0, lib.FIR_COMP_PERIODIC, out)
        assert status == lib.FIR_E_BAD_ARG
