"""CFFI harness for libfirisk.

Parses include/firisk.h with the C preprocessor and dlopens the built
shared library. Nothing is compiled, so tests run as fast as Python
starts, and they exercise the real C ABI rather than a binding layer
sitting on top of it.
"""

import ctypes
import os
import subprocess
import sys

from cffi import FFI

HERE = os.path.abspath(os.path.dirname(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
HEADER = os.path.join(ROOT, "include", "firisk.h")


def _find_library():
    """Locate libfirisk in a build tree, or via FIRISK_LIBRARY."""
    override = os.environ.get("FIRISK_LIBRARY")
    if override:
        if not os.path.exists(override):
            raise RuntimeError(f"FIRISK_LIBRARY points at nothing: {override}")
        return override

    if sys.platform == "win32":
        names = ["firisk.dll"]
    elif sys.platform == "darwin":
        names = ["libfirisk.dylib"]
    else:
        names = ["libfirisk.so"]

    # Preset build dirs first, then a plain ./build, then an install.
    roots = [
        os.path.join(ROOT, "build", "gcc-debug", "src"),
        os.path.join(ROOT, "build", "gcc-release", "src"),
        os.path.join(ROOT, "build", "clang-asan", "src"),
        os.path.join(ROOT, "build", "src"),
        os.path.join(ROOT, "build"),
    ]

    found = []
    for root in roots:
        for name in names:
            path = os.path.join(root, name)
            if os.path.exists(path):
                found.append(path)

    if not found:
        raise RuntimeError(
            "could not find libfirisk. Build it first:\n"
            "  cmake --preset gcc-debug && cmake --build --preset gcc-debug\n"
            "or set FIRISK_LIBRARY to the shared library path."
        )

    # Newest wins, so a fresh build is picked up without cleaning others.
    found.sort(key=os.path.getmtime, reverse=True)
    return found[0]


def _preprocess_header():
    """Run the C preprocessor over firisk.h with FIR_NOINCLUDE set.

    FIR_API expands to nothing and the system includes are suppressed,
    so CFFI sees only our declarations. The fixed-width and size types
    that stddef.h/stdint.h would have supplied are injected below.
    """
    cc = os.environ.get("CC", "cc")

    if sys.platform == "win32" and "cl" in os.path.basename(cc).lower():
        cmd = [cc, "/EP", "/DFIR_API=", "/DFIR_NOINCLUDE", HEADER]
    else:
        cmd = [cc, "-E", "-P", "-DFIR_API=", "-DFIR_NOINCLUDE", HEADER]

    try:
        result = subprocess.run(cmd, capture_output=True, check=True)
    except FileNotFoundError as exc:
        raise RuntimeError(
            f"no C preprocessor found (tried {cc!r}); set CC to one"
        ) from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(
            "preprocessing firisk.h failed:\n"
            + exc.stderr.decode("utf-8", "replace")
        ) from exc

    return result.stdout.decode("utf-8")


def _type_prelude():
    """Typedefs matching the running interpreter's C ABI.

    size_t is the only one that actually varies: 8 bytes on LP64 Linux
    and LLP64 Windows, 4 on 32-bit. Deriving it from ctypes rather than
    hardcoding it keeps this correct wherever the tests run.
    """
    size_t_bytes = ctypes.sizeof(ctypes.c_size_t)

    if size_t_bytes == 8:
        size_t = "unsigned long long"
    elif size_t_bytes == 4:
        size_t = "unsigned int"
    else:
        raise RuntimeError(f"unexpected sizeof(size_t): {size_t_bytes}")

    return f"""
typedef {size_t} size_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
"""


def _build_ffi():
    ffi = FFI()
    ffi.cdef(_type_prelude())
    ffi.cdef(_preprocess_header())
    return ffi


LIBRARY_PATH = _find_library()

ffi = _build_ffi()
lib = ffi.dlopen(LIBRARY_PATH)


# --------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------

def check(status, what="call"):
    """Raise on a non-OK status, using the library's own message."""
    if status != lib.FIR_OK:
        text = ffi.string(lib.fir_strerror(status)).decode()
        raise RuntimeError(f"{what} failed: {text} ({status})")
    return status


def last_message(ctx):
    """The context's current diagnostic, copied out of its buffer.

    The pointer is only valid until the next call taking this context,
    so this must copy rather than hold it.
    """
    return ffi.string(lib.fir_context_last_message(ctx)).decode()


def make_bond_def(**kwargs):
    """A fir_bond_def_t with struct_size set and sane defaults.

    Field names match the header; anything not given keeps the default.
    """
    defaults = {
        "issue_date": 20240215,
        "first_coupon_date": 0,
        "maturity_date": 20340215,
        "coupon_rate": 0.045,
        "face": 100.0,
        "frequency": lib.FIR_FREQ_SEMIANNUAL,
        "daycount": lib.FIR_DC_THIRTY_360_BOND,
        "business_day_convention": lib.FIR_BDC_NONE,
        "end_of_month": 0,
        "calendar": ffi.NULL,
    }

    unknown = set(kwargs) - set(defaults)
    if unknown:
        raise TypeError(f"unknown bond_def fields: {sorted(unknown)}")

    defaults.update(kwargs)

    definition = ffi.new("fir_bond_def_t *")
    definition.struct_size = ffi.sizeof("fir_bond_def_t")
    for key, value in defaults.items():
        setattr(definition, key, value)

    return definition


def new_bond(ctx, **kwargs):
    """Build a bond, raising with the context's message on failure."""
    definition = make_bond_def(**kwargs)
    bond = lib.fir_bond_new(ctx, definition)
    if bond == ffi.NULL:
        raise RuntimeError(f"fir_bond_new failed: {last_message(ctx)}")
    return ffi.gc(bond, lib.fir_bond_free)


def cashflows(bond):
    """(times, amounts) as Python lists."""
    count = ffi.new("size_t *")
    check(lib.fir_bond_cashflow_count(bond, count), "cashflow_count")

    n = count[0]
    times = ffi.new("double[]", n)
    amounts = ffi.new("double[]", n)
    check(lib.fir_bond_cashflows(bond, times, amounts, n), "cashflows")

    return list(times), list(amounts)


class FlatCurve:
    """A flat continuously compounded curve implementing fir_curve_t.

    The callbacks are CFFI closures. They must be kept alive as long as
    the struct is used, so they are held as attributes here rather than
    created inline at the call site.
    """

    def __init__(self, rate=0.04, pillars=None):
        self._base = rate
        self._rate = rate
        self._pillars = list(pillars) if pillars else []
        self._pillar_bumps = {t: 0.0 for t in self._pillars}

        self._discount_cb = ffi.callback(
            "double(void *, double)", self._discount)
        self._bump_cb = ffi.callback(
            "fir_status_t(void *, double, double)", self._bump)
        self._bump_parallel_cb = ffi.callback(
            "fir_status_t(void *, double)", self._bump_parallel)
        self._reset_cb = ffi.callback(
            "fir_status_t(void *)", self._reset)

        if self._pillars:
            self._pillar_array = ffi.new("double[]", self._pillars)
        else:
            self._pillar_array = ffi.NULL

        self.handle = ffi.new("fir_curve_t *")
        self.handle.struct_size = ffi.sizeof("fir_curve_t")
        self.handle.user_data = ffi.NULL
        self.handle.discount = self._discount_cb
        self.handle.bump = self._bump_cb
        self.handle.bump_parallel = self._bump_parallel_cb
        self.handle.reset = self._reset_cb
        self.handle.pillars = self._pillar_array
        self.handle.pillar_count = len(self._pillars)

    @property
    def rate(self):
        return self._rate

    def drop_bump_support(self):
        """Null the bump callbacks, leaving discounting intact."""
        self.handle.bump = ffi.NULL
        self.handle.bump_parallel = ffi.NULL
        self.handle.reset = ffi.NULL

    # -- callbacks --

    def _discount(self, user_data, t):
        import math
        return math.exp(-self._rate * t)

    def _bump(self, user_data, tenor, bump_bp):
        for pillar in self._pillars:
            if abs(pillar - tenor) < 1e-9:
                self._pillar_bumps[pillar] += bump_bp * 1e-4
                # Flat curve: a pillar bump moves the whole level, which
                # makes key-rate durations sum to the parallel figure.
                self._rate += bump_bp * 1e-4
                return lib.FIR_OK
        return lib.FIR_E_BAD_ARG

    def _bump_parallel(self, user_data, bump_bp):
        self._rate += bump_bp * 1e-4
        return lib.FIR_OK

    def _reset(self, user_data):
        self._rate = self._base
        self._pillar_bumps = {t: 0.0 for t in self._pillars}
        return lib.FIR_OK
