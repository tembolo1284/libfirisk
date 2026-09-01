"""Shared fixtures for the libfirisk CFFI tests."""

import pytest

from testhelpers import FlatCurve, ffi, lib, new_bond


VALUATION_DATE = 20260831


@pytest.fixture(scope="function")
def ctx():
    """A context with the valuation date set, freed after each test.

    Function-scoped deliberately: the context carries mutable state
    (last error, bump size), so sharing one across tests would let a
    failure in one surface as a confusing failure in another.
    """
    handle = lib.fir_context_new()
    assert handle != ffi.NULL

    assert lib.fir_context_set_valuation_date(handle, VALUATION_DATE) == lib.FIR_OK

    yield handle

    lib.fir_context_free(handle)


@pytest.fixture(scope="function")
def calendar():
    """Weekends only, no holidays."""
    handle = lib.fir_calendar_new()
    assert handle != ffi.NULL

    yield handle

    lib.fir_calendar_free(handle)


@pytest.fixture
def bond(ctx):
    """A 4.5% semiannual bond maturing Feb 2034, 30/360."""
    return new_bond(ctx)


@pytest.fixture
def zero_bond(ctx):
    """A zero-coupon bond maturing Feb 2034."""
    return new_bond(
        ctx,
        coupon_rate=0.0,
        frequency=lib.FIR_FREQ_ZERO,
        daycount=lib.FIR_DC_ACT_365F,
    )


@pytest.fixture
def flat_curve():
    """A flat 4% continuously compounded curve with standard pillars."""
    return FlatCurve(rate=0.04, pillars=[1.0, 2.0, 3.0, 5.0, 7.0, 10.0])


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "abi: exercises struct_size or version compatibility")
