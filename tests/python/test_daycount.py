"""Calendar, schedule, and accrual behaviour through the C ABI."""

import pytest

from testhelpers import cashflows, ffi, lib, make_bond_def, new_bond


class TestCalendar:
    def test_weekends_are_not_business_days(self, calendar):
        # 2026-08-29 Sat, 2026-08-30 Sun, 2026-08-31 Mon.
        assert lib.fir_calendar_is_business_day(calendar, 20260829) == 0
        assert lib.fir_calendar_is_business_day(calendar, 20260830) == 0
        assert lib.fir_calendar_is_business_day(calendar, 20260831) == 1

    def test_holidays_are_excluded(self, calendar):
        dates = ffi.new("fir_date_t[]", [20261225, 20270101])
        assert lib.fir_calendar_set_holidays(calendar, dates, 2) == lib.FIR_OK

        # 2026-12-25 is a Friday, so the holiday is what excludes it.
        assert lib.fir_calendar_is_business_day(calendar, 20261225) == 0
        assert lib.fir_calendar_is_business_day(calendar, 20261224) == 1

    def test_holidays_are_copied_not_borrowed(self, calendar):
        dates = ffi.new("fir_date_t[]", [20261225])
        assert lib.fir_calendar_set_holidays(calendar, dates, 1) == lib.FIR_OK

        # Drop the caller's array; the calendar must still know.
        del dates
        assert lib.fir_calendar_is_business_day(calendar, 20261225) == 0

    def test_invalid_holiday_is_rejected(self, calendar):
        dates = ffi.new("fir_date_t[]", [20260230])
        status = lib.fir_calendar_set_holidays(calendar, dates, 1)
        assert status == lib.FIR_E_BAD_DATE

    def test_empty_holiday_list(self, calendar):
        assert lib.fir_calendar_set_holidays(calendar, ffi.NULL, 0) == lib.FIR_OK

    def test_weekend_mask_friday_saturday(self, calendar):
        status = lib.fir_calendar_set_weekend(calendar, lib.FIR_WEEKEND_FRI_SAT)
        assert status == lib.FIR_OK

        # 2026-08-28 Fri, 2026-08-30 Sun.
        assert lib.fir_calendar_is_business_day(calendar, 20260828) == 0
        assert lib.fir_calendar_is_business_day(calendar, 20260830) == 1

    def test_all_days_weekend_is_rejected(self, calendar):
        assert lib.fir_calendar_set_weekend(calendar, 0x7F) == lib.FIR_E_BAD_ARG

    def test_no_weekend_mask(self, calendar):
        assert lib.fir_calendar_set_weekend(calendar, lib.FIR_WEEKEND_NONE) == lib.FIR_OK
        assert lib.fir_calendar_is_business_day(calendar, 20260829) == 1


class TestValuationDate:
    def test_impossible_dates_are_rejected(self, ctx):
        for bad in (20260230, 20260431, 20250229, 20261301, 20260100):
            assert lib.fir_context_set_valuation_date(ctx, bad) == lib.FIR_E_BAD_DATE

    def test_leap_day_is_accepted(self, ctx):
        assert lib.fir_context_set_valuation_date(ctx, 20240229) == lib.FIR_OK

    def test_century_non_leap(self, ctx):
        # 1900 is not a leap year; 2000 is.
        assert lib.fir_context_set_valuation_date(ctx, 19000229) == lib.FIR_E_BAD_DATE
        assert lib.fir_context_set_valuation_date(ctx, 20000229) == lib.FIR_OK


class TestSchedule:
    def test_semiannual_cashflow_count(self, bond):
        times, amounts = cashflows(bond)

        # Feb 2024 to Feb 2034 semiannual, valued 2026-08-31: the
        # remaining payments run Feb 2027 through Feb 2034.
        assert len(times) == 15

    def test_times_are_increasing_and_positive(self, bond):
        times, _ = cashflows(bond)

        assert times[0] > 0.0
        assert all(b > a for a, b in zip(times, times[1:]))

    def test_final_flow_carries_redemption(self, bond):
        _, amounts = cashflows(bond)

        # 4.5% semiannual on 100 face is 2.25 a coupon.
        assert amounts[-1] == pytest.approx(102.25, abs=0.01)
        assert all(a == pytest.approx(2.25, abs=0.01) for a in amounts[:-1])

    def test_annual_has_half_the_flows(self, ctx):
        annual = new_bond(ctx, frequency=lib.FIR_FREQ_ANNUAL)
        times, _ = cashflows(annual)
        assert len(times) == 8   # Feb 2027 .. Feb 2034

    def test_quarterly_has_double(self, ctx):
        quarterly = new_bond(ctx, frequency=lib.FIR_FREQ_QUARTERLY)
        times, _ = cashflows(quarterly)
        assert len(times) == 30

    def test_zero_coupon_has_one_flow(self, zero_bond):
        times, amounts = cashflows(zero_bond)

        assert len(times) == 1
        assert amounts[0] == pytest.approx(100.0)

    def test_maturity_before_valuation_is_rejected(self, ctx):
        definition = make_bond_def(maturity_date=20250215)
        assert lib.fir_bond_new(ctx, definition) == ffi.NULL
        assert lib.fir_context_last_status(ctx) == lib.FIR_E_BAD_SCHEDULE

    def test_buffer_too_small(self, bond):
        count = ffi.new("size_t *")
        lib.fir_bond_cashflow_count(bond, count)

        times = ffi.new("double[]", count[0])
        amounts = ffi.new("double[]", count[0])

        status = lib.fir_bond_cashflows(bond, times, amounts, count[0] - 1)
        assert status == lib.FIR_E_BUFFER_TOO_SMALL

    def test_either_output_may_be_null(self, bond):
        count = ffi.new("size_t *")
        lib.fir_bond_cashflow_count(bond, count)

        times = ffi.new("double[]", count[0])
        status = lib.fir_bond_cashflows(bond, times, ffi.NULL, count[0])
        assert status == lib.FIR_OK


class TestDaycount:
    @pytest.mark.parametrize("daycount", [
        lib.FIR_DC_ACT_360,
        lib.FIR_DC_ACT_365F,
        lib.FIR_DC_ACT_ACT_ISDA,
        lib.FIR_DC_ACT_ACT_ICMA,
        lib.FIR_DC_THIRTY_360_BOND,
        lib.FIR_DC_THIRTY_E_360,
    ])
    def test_every_convention_builds(self, ctx, daycount):
        bond = new_bond(ctx, daycount=daycount)
        times, amounts = cashflows(bond)

        assert len(times) == 15
        assert all(a > 0.0 for a in amounts)

    def test_act_360_pays_more_than_thirty_360(self, ctx):
        # A semiannual period is ~181-184 actual days over a 360
        # denominator, so ACT/360 accrues more than a flat 180/360.
        act = new_bond(ctx, daycount=lib.FIR_DC_ACT_360)
        thirty = new_bond(ctx, daycount=lib.FIR_DC_THIRTY_360_BOND)

        _, act_amounts = cashflows(act)
        _, thirty_amounts = cashflows(thirty)

        assert act_amounts[0] > thirty_amounts[0]

    def test_icma_gives_exact_half_coupons(self, ctx):
        # ICMA divides by the period's own length times the frequency,
        # so a regular semiannual period is exactly 0.5 years.
        bond = new_bond(ctx, daycount=lib.FIR_DC_ACT_ACT_ICMA)
        _, amounts = cashflows(bond)

        assert amounts[0] == pytest.approx(2.25, abs=1e-10)

    def test_unknown_daycount_is_rejected(self, ctx):
        definition = make_bond_def(daycount=999)
        assert lib.fir_bond_new(ctx, definition) == ffi.NULL


class TestAccrued:
    def test_accrued_is_non_negative(self, bond):
        out = ffi.new("double *")
        assert lib.fir_bond_accrued(bond, out) == lib.FIR_OK
        assert out[0] >= 0.0

    def test_accrued_below_one_coupon(self, bond):
        out = ffi.new("double *")
        lib.fir_bond_accrued(bond, out)

        # Cannot exceed a full 4.5%/2 coupon on 100 face.
        assert out[0] < 2.25

    def test_valuation_on_a_coupon_date_accrues_nothing(self, ctx):
        assert lib.fir_context_set_valuation_date(ctx, 20260815) == lib.FIR_OK
        bond = new_bond(ctx, issue_date=20240215, maturity_date=20340215)

        out = ffi.new("double *")
        lib.fir_bond_accrued(bond, out)
        assert out[0] == pytest.approx(0.0, abs=1e-12)

    def test_zero_coupon_accrues_nothing(self, zero_bond):
        out = ffi.new("double *")
        assert lib.fir_bond_accrued(zero_bond, out) == lib.FIR_OK
        assert out[0] == 0.0
