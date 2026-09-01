"""Curve risk: effective duration, curve DV01, and key rate durations."""

import pytest

from testhelpers import FlatCurve, ffi, lib, new_bond


PILLARS = [1.0, 2.0, 3.0, 5.0, 7.0, 10.0]


def effective_duration(ctx, bond, curve):
    out = ffi.new("double *")
    status = lib.fir_bond_effective_duration(ctx, bond, curve.handle, out)
    assert status == lib.FIR_OK, lib.fir_context_last_message(ctx)
    return out[0]


def effective_convexity(ctx, bond, curve):
    out = ffi.new("double *")
    status = lib.fir_bond_effective_convexity(ctx, bond, curve.handle, out)
    assert status == lib.FIR_OK
    return out[0]


def curve_dv01(ctx, bond, curve):
    out = ffi.new("double *")
    status = lib.fir_bond_curve_dv01(ctx, bond, curve.handle, out)
    assert status == lib.FIR_OK
    return out[0]


def key_rates(ctx, bond, curve, tenors=None):
    tenors = PILLARS if tenors is None else tenors

    array = ffi.new("double[]", tenors)
    out = ffi.new("double[]", len(tenors))

    status = lib.fir_bond_key_rate_durations(
        ctx, bond, curve.handle, array, out, len(tenors))
    assert status == lib.FIR_OK, lib.fir_context_last_message(ctx)

    return list(out)


class TestEffectiveDuration:
    def test_positive_and_reasonable(self, ctx, bond, flat_curve):
        assert 0.0 < effective_duration(ctx, bond, flat_curve) < 10.0

    def test_close_to_analytic_modified_duration(self, ctx, bond, flat_curve):
        # A flat continuous curve at 4% is the same discounting as a
        # continuous yield of 4%, so the two measures should agree to
        # the accuracy of the central difference.
        analytic = ffi.new("double *")
        lib.fir_bond_modified_duration(
            bond, 0.04, lib.FIR_COMP_CONTINUOUS, analytic)

        assert effective_duration(ctx, bond, flat_curve) == pytest.approx(
            analytic[0], rel=1e-6)

    def test_curve_is_restored_after_bumping(self, ctx, bond, flat_curve):
        effective_duration(ctx, bond, flat_curve)
        assert flat_curve.rate == pytest.approx(0.04, abs=1e-15)

    def test_restored_even_across_several_calls(self, ctx, bond, flat_curve):
        for _ in range(5):
            effective_duration(ctx, bond, flat_curve)
            effective_convexity(ctx, bond, flat_curve)
            curve_dv01(ctx, bond, flat_curve)

        assert flat_curve.rate == pytest.approx(0.04, abs=1e-15)

    def test_bump_size_changes_the_estimate_slightly(self, ctx, bond, flat_curve):
        lib.fir_context_set_bump_size(ctx, 1.0)
        fine = effective_duration(ctx, bond, flat_curve)

        lib.fir_context_set_bump_size(ctx, 100.0)
        coarse = effective_duration(ctx, bond, flat_curve)

        # Same measure, so close, but a 100bp central difference picks
        # up convexity the 1bp one does not.
        assert fine == pytest.approx(coarse, rel=1e-2)
        assert fine != coarse

    def test_no_bump_support_is_reported(self, ctx, bond, flat_curve):
        flat_curve.drop_bump_support()

        out = ffi.new("double *")
        status = lib.fir_bond_effective_duration(
            ctx, bond, flat_curve.handle, out)
        assert status == lib.FIR_E_NO_BUMP_SUPPORT

    def test_failure_leaves_a_message_on_the_context(self, ctx, bond, flat_curve):
        flat_curve.drop_bump_support()

        out = ffi.new("double *")
        lib.fir_bond_effective_duration(ctx, bond, flat_curve.handle, out)

        message = ffi.string(lib.fir_context_last_message(ctx)).decode()
        assert message
        assert lib.fir_context_last_status(ctx) == lib.FIR_E_NO_BUMP_SUPPORT


class TestEffectiveConvexity:
    def test_positive_for_a_vanilla_bond(self, ctx, bond, flat_curve):
        assert effective_convexity(ctx, bond, flat_curve) > 0.0

    def test_close_to_analytic(self, ctx, bond, flat_curve):
        analytic = ffi.new("double *")
        lib.fir_bond_convexity(bond, 0.04, lib.FIR_COMP_CONTINUOUS, analytic)

        assert effective_convexity(ctx, bond, flat_curve) == pytest.approx(
            analytic[0], rel=1e-4)


class TestCurveDv01:
    def test_positive(self, ctx, bond, flat_curve):
        assert curve_dv01(ctx, bond, flat_curve) > 0.0

    def test_consistent_with_effective_duration(self, ctx, bond, flat_curve):
        priced = ffi.new("double *")
        lib.fir_bond_price_from_curve(bond, flat_curve.handle, priced)

        accrued = ffi.new("double *")
        lib.fir_bond_accrued(bond, accrued)
        dirty = priced[0] + accrued[0]

        expected = effective_duration(ctx, bond, flat_curve) * dirty * 1e-4
        assert curve_dv01(ctx, bond, flat_curve) == pytest.approx(
            expected, rel=1e-6)


class TestKeyRateDurations:
    def test_one_value_per_tenor(self, ctx, bond, flat_curve):
        assert len(key_rates(ctx, bond, flat_curve)) == len(PILLARS)

    def test_all_positive_for_a_vanilla_bond(self, ctx, bond, flat_curve):
        assert all(k > 0.0 for k in key_rates(ctx, bond, flat_curve))

    def test_curve_restored_afterwards(self, ctx, bond, flat_curve):
        key_rates(ctx, bond, flat_curve)
        assert flat_curve.rate == pytest.approx(0.04, abs=1e-15)

    def test_unknown_tenor_is_reported(self, ctx, bond, flat_curve):
        # The test curve rejects a tenor that is not one of its pillars.
        array = ffi.new("double[]", [4.0])
        out = ffi.new("double[]", 1)

        status = lib.fir_bond_key_rate_durations(
            ctx, bond, flat_curve.handle, array, out, 1)
        assert status == lib.FIR_E_BAD_ARG

    def test_negative_tenor_is_rejected(self, ctx, bond, flat_curve):
        array = ffi.new("double[]", [-1.0])
        out = ffi.new("double[]", 1)

        status = lib.fir_bond_key_rate_durations(
            ctx, bond, flat_curve.handle, array, out, 1)
        assert status == lib.FIR_E_BAD_ARG

    def test_zero_tenors_is_a_no_op(self, ctx, bond, flat_curve):
        out = ffi.new("double[]", 1)
        status = lib.fir_bond_key_rate_durations(
            ctx, bond, flat_curve.handle, ffi.NULL, out, 0)
        assert status == lib.FIR_OK

    def test_no_bump_support_is_reported(self, ctx, bond, flat_curve):
        flat_curve.drop_bump_support()

        array = ffi.new("double[]", PILLARS)
        out = ffi.new("double[]", len(PILLARS))

        status = lib.fir_bond_key_rate_durations(
            ctx, bond, flat_curve.handle, array, out, len(PILLARS))
        assert status == lib.FIR_E_NO_BUMP_SUPPORT
