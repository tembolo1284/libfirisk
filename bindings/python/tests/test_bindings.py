"""Tests for the nanobind layer.

These cover the binding surface — type coercion, exceptions, array
returns, lifetimes. The numerical behaviour is already pinned by the
CFFI suite against the same C ABI, so it is not repeated here.
"""

import datetime

import numpy as np
import pytest

import firisk
from firisk import Compounding, DayCount, Frequency


VALUATION = 20260831


@pytest.fixture
def ctx():
    return firisk.Context(valuation_date=VALUATION)


@pytest.fixture
def bond(ctx):
    return firisk.Bond(
        ctx,
        issue_date=20240215,
        maturity_date=20340215,
        coupon_rate=0.045,
    )


@pytest.fixture
def flat():
    return firisk.FlatCurve(0.04)


@pytest.fixture
def zero_curve():
    tenors = [1.0, 2.0, 3.0, 5.0, 7.0, 10.0]
    zeros = [0.038, 0.039, 0.040, 0.041, 0.042, 0.043]
    return firisk.ZeroCurve(tenors, zeros)


class TestDateCoercion:
    def test_accepts_int(self):
        assert firisk.to_yyyymmdd(20260831) == 20260831

    def test_accepts_date(self):
        assert firisk.to_yyyymmdd(datetime.date(2026, 8, 31)) == 20260831

    def test_accepts_datetime(self):
        value = datetime.datetime(2026, 8, 31, 14, 30)
        assert firisk.to_yyyymmdd(value) == 20260831

    def test_accepts_iso_string(self):
        assert firisk.to_yyyymmdd("2026-08-31") == 20260831

    def test_rejects_bool(self):
        with pytest.raises(TypeError):
            firisk.to_yyyymmdd(True)

    def test_rejects_nonsense(self):
        with pytest.raises(TypeError):
            firisk.to_yyyymmdd(object())

    def test_round_trip(self):
        original = datetime.date(2034, 2, 15)
        assert firisk.from_yyyymmdd(firisk.to_yyyymmdd(original)) == original

    def test_bond_accepts_mixed_date_types(self, ctx):
        bond = firisk.Bond(
            ctx,
            issue_date="2024-02-15",
            maturity_date=datetime.date(2034, 2, 15),
            coupon_rate=0.045,
        )
        assert bond.cashflow_count == 15


class TestExceptions:
    def test_bad_valuation_date_raises(self):
        with pytest.raises(ValueError, match="valid"):
            firisk.Context(valuation_date=20260230)

    def test_maturity_before_valuation_raises(self, ctx):
        with pytest.raises(ValueError):
            firisk.Bond(ctx, issue_date=20240215,
                        maturity_date=20250215, coupon_rate=0.045)

    def test_absurd_coupon_raises(self, ctx):
        # 4.5 where 0.045 was meant.
        with pytest.raises(ValueError, match="decimal"):
            firisk.Bond(ctx, issue_date=20240215,
                        maturity_date=20340215, coupon_rate=4.5)

    def test_bad_bump_size_raises(self, ctx):
        with pytest.raises(ValueError):
            ctx.bump_size = 0.0

    def test_curve_without_bumps_raises(self, ctx, bond):
        class DiscountOnly:
            def discount(self, t):
                return 1.0 / (1.0 + 0.04 * t)

        curve = firisk.PyCurve(DiscountOnly())

        with pytest.raises(ValueError, match="bump"):
            bond.effective_duration(ctx, curve)

    def test_unreachable_price_raises(self, bond):
        with pytest.raises(RuntimeError, match="converge"):
            bond.yield_from_price(-50.0)


class TestArrays:
    def test_cashflows_are_numpy_arrays(self, bond):
        times, amounts = bond.cashflows()

        assert isinstance(times, np.ndarray)
        assert times.dtype == np.float64
        assert times.shape == (bond.cashflow_count,)
        assert amounts.shape == times.shape

    def test_cashflow_values(self, bond):
        times, amounts = bond.cashflows()

        assert np.all(np.diff(times) > 0)
        assert amounts[-1] == pytest.approx(102.25, abs=0.01)

    def test_arrays_survive_the_bond(self, ctx):
        bond = firisk.Bond(ctx, issue_date=20240215,
                           maturity_date=20340215, coupon_rate=0.045)
        times, amounts = bond.cashflows()
        expected = times.copy()

        del bond

        # The capsule owns the buffer, so it outlives the bond.
        assert np.array_equal(times, expected)

    def test_key_rate_durations_is_an_array(self, ctx, bond, zero_curve):
        krd = bond.key_rate_durations(ctx, zero_curve, zero_curve.tenors)

        assert isinstance(krd, np.ndarray)
        assert krd.shape == (len(zero_curve.tenors),)
        assert np.all(krd > 0)

    def test_empty_tenors_gives_empty_array(self, ctx, bond, zero_curve):
        krd = bond.key_rate_durations(ctx, zero_curve, [])
        assert krd.shape == (0,)


class TestNativeCurves:
    def test_flat_curve_discount(self, flat):
        assert flat.discount(1.0) == pytest.approx(np.exp(-0.04))

    def test_flat_curve_prices(self, bond, flat):
        assert bond.price_from_curve(flat) > 0.0

    def test_flat_curve_restores_after_risk(self, ctx, bond, flat):
        bond.effective_duration(ctx, flat)
        assert flat.rate == pytest.approx(0.04)

    def test_zero_curve_interpolates(self, zero_curve):
        # Midway between the 2y at 3.9% and 3y at 4.0%.
        assert zero_curve.zero_rate(2.5) == pytest.approx(0.0395)

    def test_zero_curve_extrapolates_flat(self, zero_curve):
        assert zero_curve.zero_rate(0.1) == pytest.approx(0.038)
        assert zero_curve.zero_rate(30.0) == pytest.approx(0.043)

    def test_zero_curve_rejects_mismatched_lengths(self):
        with pytest.raises(ValueError, match="same length"):
            firisk.ZeroCurve([1.0, 2.0], [0.04])

    def test_zero_curve_rejects_unsorted_tenors(self):
        with pytest.raises(ValueError, match="increasing"):
            firisk.ZeroCurve([2.0, 1.0], [0.04, 0.04])

    def test_key_rates_localise(self, ctx, bond, zero_curve):
        # A short bond should carry almost no 10y sensitivity, which is
        # the whole point of interpolated pillar bumps.
        short = firisk.Bond(ctx, issue_date=20240215,
                            maturity_date=20280215, coupon_rate=0.045)

        krd = short.key_rates(zero_curve)
        assert krd[-1] < krd[0]

    def test_key_rates_sum_near_effective_duration(self, ctx, bond, zero_curve):
        krd = bond.key_rates(zero_curve)
        effective = bond.effective_duration(ctx, zero_curve)

        # Localised bumps mean the sum reconstructs a parallel shift, up
        # to the flat extrapolation past the last pillar.
        assert krd.sum() == pytest.approx(effective, rel=0.05)


class TestPyCurve:
    def test_wraps_a_python_object(self, bond):
        class Flat:
            def __init__(self, rate):
                self.rate = rate
                self._base = rate

            def discount(self, t):
                return np.exp(-self.rate * t)

            def bump_parallel(self, bp):
                self.rate += bp * 1e-4

            def bump(self, tenor, bp):
                self.rate += bp * 1e-4

            def reset(self):
                self.rate = self._base

        native = firisk.FlatCurve(0.04)
        wrapped = firisk.PyCurve(Flat(0.04))

        assert bond.price_from_curve(wrapped) == pytest.approx(
            bond.price_from_curve(native), rel=1e-12)

    def test_raising_discount_becomes_an_error(self, bond):
        class Broken:
            def discount(self, t):
                raise RuntimeError("nope")

        with pytest.raises((ValueError, RuntimeError)):
            bond.price_from_curve(firisk.PyCurve(Broken()))

    def test_requires_a_discount_method(self):
        with pytest.raises(ValueError, match="discount"):
            firisk.PyCurve(object())


class TestCalendar:
    def test_weekends_by_default(self):
        cal = firisk.Calendar()
        assert cal.is_business_day("2026-08-31")
        assert not cal.is_business_day("2026-08-29")

    def test_holidays_from_constructor(self):
        cal = firisk.Calendar(holidays=["2026-12-25"])
        assert not cal.is_business_day("2026-12-25")
        assert cal.is_business_day("2026-12-24")

    def test_bond_uses_the_calendar(self, ctx):
        cal = firisk.Calendar(holidays=["2027-02-15"])
        bond = firisk.Bond(
            ctx,
            issue_date=20240215,
            maturity_date=20340215,
            coupon_rate=0.045,
            business_day_convention=firisk.BusinessDayConvention.FOLLOWING,
            calendar=cal,
        )
        assert bond.cashflow_count == 15


class TestLifetimes:
    def test_bond_outlives_an_inline_context(self):
        # keep_alive ties the context to the bond, so this must not
        # leave the bond holding a freed context.
        bond = firisk.Bond(
            firisk.Context(valuation_date=VALUATION),
            issue_date=20240215,
            maturity_date=20340215,
            coupon_rate=0.045,
        )
        assert bond.price_from_yield(0.045) > 0.0

    def test_many_bonds_on_one_context(self, ctx):
        bonds = [
            firisk.Bond(ctx, issue_date=20240215,
                        maturity_date=20340215, coupon_rate=c / 1000.0)
            for c in range(10, 90, 10)
        ]
        prices = [b.price_from_yield(0.045) for b in bonds]
        assert all(b > a for a, b in zip(prices, prices[1:]))


class TestConvenience:
    def test_risk_dict(self, bond):
        risk = bond.risk(0.045)

        assert set(risk) == {
            "clean_price", "accrued", "macaulay_duration",
            "modified_duration", "convexity", "dv01",
        }
        assert risk["modified_duration"] == pytest.approx(
            risk["macaulay_duration"] / 1.0225, rel=1e-12)

    def test_curve_risk_dict(self, bond, zero_curve):
        risk = bond.curve_risk(zero_curve)
        assert risk["effective_duration"] > 0.0
        assert risk["dv01"] > 0.0

    def test_dirty_equals_clean_plus_accrued(self, bond):
        assert bond.dirty_price_from_yield(0.045) == pytest.approx(
            bond.price_from_yield(0.045) + bond.accrued)

    def test_repr(self, bond):
        assert "2034-02-15" in repr(bond)
        assert "4.5" in repr(bond)

    def test_version(self):
        assert firisk.version() == "0.1.0"
