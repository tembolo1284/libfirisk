"""Fixed income risk analytics.

Thin Python layer over the compiled ``_firisk`` extension, which in turn
sits on the ``libfirisk`` C ABI. Everything here is either a re-export or
a convenience that would be awkward to express in C++.

    >>> import firisk
    >>> ctx = firisk.Context(valuation_date=20260831)
    >>> bond = firisk.Bond(ctx, issue_date=20240215,
    ...                    maturity_date=20340215, coupon_rate=0.045)
    >>> round(bond.price_from_yield(0.05), 4)
    96.6...
"""

from __future__ import annotations

import datetime as _datetime
from typing import Iterable, Sequence

from . import _firisk
from ._firisk import (
    BusinessDayConvention,
    Calendar as _Calendar,
    Compounding,
    Curve,
    DayCount,
    FlatCurve,
    Frequency,
    PyCurve,
    Weekend,
    ZeroCurve,
)

__all__ = [
    "Bond",
    "BusinessDayConvention",
    "Calendar",
    "Compounding",
    "Context",
    "Curve",
    "DayCount",
    "FlatCurve",
    "Frequency",
    "PyCurve",
    "Weekend",
    "ZeroCurve",
    "to_yyyymmdd",
    "from_yyyymmdd",
    "version",
]

__version__ = _firisk.__version__


def version() -> str:
    """Version of the underlying native library."""
    return _firisk.version()


# --------------------------------------------------------------------
# dates
# --------------------------------------------------------------------

def to_yyyymmdd(value) -> int:
    """Coerce a date to the integer form the C ABI uses.

    Accepts ``datetime.date``, ``datetime.datetime``, an ISO string, or
    an integer that is already in YYYYMMDD form.
    """
    if isinstance(value, bool):
        raise TypeError("a bool is not a date")

    if isinstance(value, int):
        return value

    if isinstance(value, _datetime.datetime):
        value = value.date()

    if isinstance(value, _datetime.date):
        return value.year * 10000 + value.month * 100 + value.day

    if isinstance(value, str):
        parsed = _datetime.date.fromisoformat(value)
        return parsed.year * 10000 + parsed.month * 100 + parsed.day

    raise TypeError(f"cannot read {type(value).__name__} as a date")


def from_yyyymmdd(value: int) -> _datetime.date:
    """Inverse of :func:`to_yyyymmdd`."""
    return _datetime.date(value // 10000, (value // 100) % 100, value % 100)

def _enum_value(value):
    """Pass a nanobind enum through unchanged; coerce anything else.

    nanobind's nb::enum_ types are not IntEnum and have no __int__, so
    int() on them raises. They convert to the underlying C enum on their
    own when handed to a bound function, so the right move is to leave
    them alone and only coerce genuine Python ints.
    """
    if isinstance(value, int):
        return int(value)
    return value
# --------------------------------------------------------------------
# calendar
# --------------------------------------------------------------------

class Calendar(_Calendar):
    """Business day calendar.

    The library ships no holiday tables — a table compiled into a shared
    object is stale before its first calendar change. Supply your own.

        >>> cal = Calendar(holidays=["2026-12-25", "2027-01-01"])
        >>> cal.is_business_day("2026-12-25")
        False
    """

    def __init__(self,
                 holidays: Iterable = (),
                 weekend: int = Weekend.SAT_SUN):
        super().__init__()

        if weekend != Weekend.SAT_SUN:
            self.set_weekend(int(weekend))

        holidays = list(holidays)
        if holidays:
            self.set_holidays([to_yyyymmdd(h) for h in holidays])

    def is_business_day(self, date) -> bool:
        return super().is_business_day(to_yyyymmdd(date))

    def set_holidays(self, dates: Iterable) -> None:
        super().set_holidays([to_yyyymmdd(d) for d in dates])


# --------------------------------------------------------------------
# context
# --------------------------------------------------------------------

class Context(_firisk.Context):
    """Holds the valuation date and numerical settings.

    Not thread safe: it carries mutable error state, so give each thread
    its own rather than sharing one.
    """

    def __init__(self,
                 valuation_date=None,
                 bump_size: float | None = None,
                 solver_tolerance: float | None = None,
                 solver_max_iter: int | None = None):
        super().__init__()

        if valuation_date is not None:
            self.valuation_date = valuation_date
        if bump_size is not None:
            self.bump_size = bump_size
        if solver_tolerance is not None:
            self.set_solver_tolerance(solver_tolerance)
        if solver_max_iter is not None:
            self.set_solver_max_iter(solver_max_iter)

    @property
    def valuation_date(self) -> int:
        return self._valuation_date

    @valuation_date.setter
    def valuation_date(self, value) -> None:
        as_int = to_yyyymmdd(value)
        self.set_valuation_date(as_int)
        self._valuation_date = as_int

    @property
    def bump_size(self) -> float:
        return getattr(self, "_bump_size", 1.0)

    @bump_size.setter
    def bump_size(self, bp: float) -> None:
        self.set_bump_size(bp)
        self._bump_size = bp


# --------------------------------------------------------------------
# bond
# --------------------------------------------------------------------

class Bond(_firisk.Bond):
    """A fixed rate bullet bond.

    Dates may be given as ``datetime.date``, an ISO string, or a
    YYYYMMDD integer. The schedule and cashflows are built once here, so
    repeated pricing and risk calls do no schedule work.
    """

    def __init__(self,
                 ctx: Context,
                 issue_date,
                 maturity_date,
                 coupon_rate: float,
                 face: float = 100.0,
                 frequency: int = Frequency.SEMIANNUAL,
                 daycount: int = DayCount.THIRTY_360_BOND,
                 business_day_convention: int = BusinessDayConvention.NONE,
                 first_coupon_date=None,
                 end_of_month: bool = False,
                 calendar: Calendar | None = None):
        super().__init__(
            ctx,
            to_yyyymmdd(issue_date),
            to_yyyymmdd(maturity_date),
            coupon_rate,
            face,
            int(frequency),
            int(daycount),
            int(business_day_convention),
            0 if first_coupon_date is None else to_yyyymmdd(first_coupon_date),
            end_of_month,
            calendar,
        )

        self._ctx = ctx
        self._coupon_rate = coupon_rate
        self._face = face
        self._maturity_date = to_yyyymmdd(maturity_date)

    # -- convenience over the compiled methods --

    def dirty_price_from_yield(self, yield_: float,
                               compounding: int = Compounding.PERIODIC) -> float:
        """Clean price plus accrued."""
        return self.price_from_yield(yield_, compounding) + self.accrued

    def risk(self, yield_: float,
             compounding: int = Compounding.PERIODIC) -> dict:
        """Every analytic measure at one yield, as a dict.

        The native side computes these from a single pass over the
        cashflows, but exposes them one at a time; this is four calls,
        so prefer the individual methods in a tight loop.
        """
        return {
            "clean_price": self.price_from_yield(yield_, compounding),
            "accrued": self.accrued,
            "macaulay_duration": self.macaulay_duration(yield_, compounding),
            "modified_duration": self.modified_duration(yield_, compounding),
            "convexity": self.convexity(yield_, compounding),
            "dv01": self.dv01(yield_, compounding),
        }

    def curve_risk(self, curve: Curve, ctx: Context | None = None) -> dict:
        """Effective duration, convexity, and DV01 off a curve.

        Requires the curve to support parallel bumping and reset.
        """
        ctx = self._ctx if ctx is None else ctx
        return {
            "clean_price": self.price_from_curve(curve),
            "effective_duration": self.effective_duration(ctx, curve),
            "effective_convexity": self.effective_convexity(ctx, curve),
            "dv01": self.curve_dv01(ctx, curve),
        }

    def key_rates(self, curve: Curve,
                  tenors: Sequence[float] | None = None,
                  ctx: Context | None = None):
        """Key rate durations, defaulting to the curve's own pillars."""
        ctx = self._ctx if ctx is None else ctx

        if tenors is None:
            tenors = getattr(curve, "tenors", None)
            if tenors is None:
                raise ValueError(
                    "this curve exposes no pillars; pass tenors explicitly")

        return self.key_rate_durations(ctx, curve, list(tenors))

    def cashflow_frame(self):
        """Cashflows as a pandas DataFrame, if pandas is installed."""
        try:
            import pandas as pd
        except ImportError as exc:
            raise ImportError(
                "cashflow_frame needs pandas; use cashflows() for arrays"
            ) from exc

        times, amounts = self.cashflows()
        return pd.DataFrame({"time": times, "amount": amounts})

    def __repr__(self) -> str:
        return (f"Bond(maturity={from_yyyymmdd(self._maturity_date)}, "
                f"coupon={self._coupon_rate:.4%}, face={self._face:g}, "
                f"cashflows={self.cashflow_count})")
