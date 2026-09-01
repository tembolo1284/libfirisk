# libfirisk

A C++20 fixed income risk library behind a stable C ABI — bond schedules, day
counts, yield and curve pricing, and both analytic and bump-and-revalue risk.

The library is built as a shared object with a single public header. The C++
never crosses the boundary: callers see opaque handles and plain functions, so
the same `.so` is usable from C, Python, Excel, or anything else that can call
into a C library. The design follows Armin Ronacher's
[Beautiful Native Libraries](https://lucumr.pocoo.org/2013/8/18/beautiful-native-libraries/).

For the finance behind the numbers, see [docs/CONCEPTS.md](docs/CONCEPTS.md).

## What it does

- Coupon schedule generation with stub handling and end-of-month rolling
- Six day count conventions: ACT/360, ACT/365F, ACT/ACT ISDA, ACT/ACT ICMA,
  30/360 Bond, 30E/360
- Business day calendars with caller-supplied holidays
- Accrued interest, clean and dirty pricing
- Yield to maturity solving under simple, periodic, or continuous compounding
- Curve-based pricing against any discount function the caller supplies
- Z-spread
- Macaulay duration, modified duration, convexity, DV01
- Effective duration, effective convexity, curve DV01, key rate durations

## Quick start

```bash
git clone <your-remote> libfirisk
cd libfirisk
./build.sh
```

That configures, builds, runs the C smoke test, creates a virtualenv, runs the
CFFI suite, builds the Python extension, and runs the binding tests.

Individual stages:

```bash
./build.sh cpp        # native library + ctest
./build.sh python     # venv + CFFI tests + wheel + binding tests
./build.sh asan       # rebuild under AddressSanitizer and retest
./build.sh examples   # run the example programs
./build.sh clean
```

### C

```c
#include <stdio.h>
#include <string.h>
#include "firisk.h"

int main(void)
{
    fir_context_t *ctx = fir_context_new();
    fir_context_set_valuation_date(ctx, 20260831);

    fir_bond_def_t def;
    memset(&def, 0, sizeof def);
    def.struct_size   = sizeof def;
    def.issue_date    = 20240215;
    def.maturity_date = 20340215;
    def.coupon_rate   = 0.045;
    def.face          = 100.0;
    def.frequency     = FIR_FREQ_SEMIANNUAL;
    def.daycount      = FIR_DC_THIRTY_360_BOND;

    fir_bond_t *bond = fir_bond_new(ctx, &def);
    if (!bond) {
        fprintf(stderr, "%s\n", fir_context_last_message(ctx));
        return 1;
    }

    double price = 0.0, dmod = 0.0, dv01 = 0.0;
    fir_bond_price_from_yield(bond, 0.05, FIR_COMP_PERIODIC, &price);
    fir_bond_modified_duration(bond, 0.05, FIR_COMP_PERIODIC, &dmod);
    fir_bond_dv01(bond, 0.05, FIR_COMP_PERIODIC, &dv01);

    printf("price %.6f  D_mod %.6f  DV01 %.6f\n", price, dmod, dv01);

    fir_bond_free(bond);
    fir_context_free(ctx);
    return 0;
}
```

Build against an installed copy:

```bash
cc -o example example.c $(pkg-config --cflags --libs firisk) -lm
```

or from CMake:

```cmake
find_package(firisk REQUIRED)
target_link_libraries(myapp PRIVATE firisk::firisk)
```

### Python

```python
import firisk

ctx = firisk.Context(valuation_date="2026-08-31")

bond = firisk.Bond(
    ctx,
    issue_date="2024-02-15",
    maturity_date="2034-02-15",
    coupon_rate=0.045,
    frequency=firisk.Frequency.SEMIANNUAL,
    daycount=firisk.DayCount.THIRTY_360_BOND,
)

print(bond.risk(0.05))

curve = firisk.ZeroCurve(
    tenors=[1.0, 2.0, 3.0, 5.0, 7.0, 10.0],
    zeros=[0.038, 0.039, 0.040, 0.041, 0.042, 0.043],
)

print(bond.price_from_curve(curve))
print(bond.curve_risk(curve))
print(bond.key_rates(curve))
```

## Design

### One public header

`include/firisk.h` is the entire public surface. It compiles as C89, includes
only `stddef.h` and `stdint.h`, and both can be suppressed with `FIR_NOINCLUDE`
so binding generators can parse it standalone. Everything under `src/core/` is
C++20 and never appears in the header.

### Opaque handles

`fir_context_t`, `fir_bond_t`, and `fir_calendar_t` are declared but never
defined. Callers hold pointers and pass them back; the library casts them to the
real C++ classes internally. Because no layout is visible, the C++ side can grow,
shrink, or be rewritten without breaking any compiled caller.

### Versioned structs

Two structs *are* visible, because the caller allocates them:
`fir_bond_def_t` and `fir_curve_t`. Both carry `struct_size` as the first member.
The library accepts any size between the oldest supported layout and the current
one, so a caller compiled against 0.1 keeps working against a later library that
appended fields — it just doesn't get the new behaviour.

New fields go at the **end** only, from 0.1.0 onward.

### The curve is a callback, not a type

The library owns no curve. `fir_curve_t` is a small vtable the caller fills in:

```c
double (*discount)(void *user_data, double t);                        /* required */
fir_status_t (*bump)(void *user_data, double tenor, double bump_bp);  /* optional */
fir_status_t (*bump_parallel)(void *user_data, double bump_bp);       /* optional */
fir_status_t (*reset)(void *user_data);                               /* required if bumping */
```

A flat rate, a bootstrapped SOFR curve, a Python object, or a separate curve
library all plug in identically, and `libfirisk` has no dependency on any of
them. If a curve supplies no bump hooks it can still price; the risk calls
return `FIR_E_NO_BUMP_SUPPORT` rather than faking a bump.

The library always calls `reset` after a bumped revaluation, including on
failure paths. A bump that survived a failed reprice would silently corrupt
every later price on that curve.

### Calendars are data, not code

No holiday tables are compiled in. A table baked into a shared object is stale
before its first calendar change, and updating one should not require a library
release. `fir_calendar_set_holidays` takes the dates from the caller and copies
them.

### Errors

Every fallible function returns a `fir_status_t`. Functions that take a context
also record a formatted diagnostic on it:

```c
if (fir_bond_effective_duration(ctx, bond, &curve, &out) != FIR_OK)
    fprintf(stderr, "%s\n", fir_context_last_message(ctx));
```

The message pointer is valid only until the next call taking that context. Copy
it if you need to keep it.

### Allocators and panics

`fir_set_allocators` replaces malloc/realloc/free process-wide. Every core class
routes `operator new` through them, and the STL containers used internally go
through a proxy allocator that does the same. Call it before the first
`fir_context_new`.

Allocation failure is fatal rather than reported: worst-case memory here is
bounded by cashflow count times pillar count, both small and both known before
allocating, and nothing in the library propagates exceptions across the ABI.
`fir_set_panic_handler` lets a host log on its own terms before the abort.

## Layout

```
include/firisk.h          the entire public API
src/capi/                 C shim — casts and guards only, no logic
src/core/                 C++20 implementation
  time/                   dates, day counts, calendars, schedules
  instrument/             bond definition and cashflow generation
  pricing/                discounting, PV derivatives, root finding
  risk/                   analytic and bump-and-revalue measures
tests/c_smoke/            proves the header compiles as C99
tests/python/             CFFI suite — tests the real ABI, no compilation
bindings/python/          nanobind extension
examples/                 runnable C programs
symbols/                  linker scripts controlling the exported surface
```

The CFFI suite is the primary test bed. It parses `firisk.h` with the C
preprocessor and dlopens the built `.so`, so it exercises the actual ABI —
struct layouts, `struct_size` handling, null guards — with no compile step
between edit and result.

## Symbol visibility

Everything is hidden by default on every platform. Only `fir_*` is exported,
enforced by a version script on Linux, an exported-symbols list on macOS, and
`__declspec(dllexport)` on Windows. Statically linked dependencies are excluded
with `--exclude-libs ALL`.

Verify with:

```bash
nm -D --defined-only build/gcc-debug/src/libfirisk.so | grep -v ' fir_'
```

That should print nothing.

## Conventions this library picked

Documented because they are defensible rather than universal, and a vendor
system that chose differently will show small persistent differences.

**Discount times use ACT/365F** regardless of the coupon day count. Time to a
cashflow measures elapsed time for the curve, not accrual. This keeps curve
interpolation consistent across instruments with different coupon conventions,
but it means a bond priced from a curve and from its own yield use different
time measures. Prices agree at the solved yield by construction.

**Z-spread is continuously compounded.** `df(t) · exp(-z·t)`. Continuous keeps
the spread independent of the bond's coupon frequency, so two bonds with
different frequencies on the same curve get comparable numbers.

**Compounding is per call, not per bond.** Every pricing and analytic risk
function takes a `fir_compounding_t`. More verbose at the call site, but it
avoids implying a bond has one true yield convention.

**Accrual dates are unadjusted; only payment dates roll.** This is the standard
bond treatment. Adjusting accrual boundaries too is a swap convention and would
change every accrued interest figure.

**Modified duration is computed as `-(1/P)·dP/dy`**, not from the
`D_mac / (1 + y/k)` identity. The identity holds exactly for periodic
compounding and the tests verify it, but deriving from the derivative means
continuous and simple compounding fall out correctly with no special case.

## Known limits

- **Fixed rate bullet bonds only.** No floaters, amortisers, callables, or
  inflation linkers.
- **Simple compounding on long bonds is fragile.** `FIR_COMP_SIMPLE` is a
  money-market convention for sub-year instruments. On a ten year bond the
  discount factor ranges over four orders of magnitude across the solver's
  bracket, and `fir_bond_yield_from_price` may return `FIR_E_NO_CONVERGENCE`.
  Use periodic or continuous.
- **Contexts are not thread safe.** They carry mutable error state and scratch
  buffers. Give each thread its own; the library has no other shared mutable
  state beyond the process-wide allocators.
- **Key rate durations are only as good as the caller's bump semantics.** The
  library asks a curve to bump a pillar; how that bump decays between pillars is
  entirely the curve's business. A curve that moves its whole level on every
  pillar bump will produce key rates that sum to N times the parallel figure.
- **Day count implementations are independently written** and have not been
  reconciled against a vendor system. The 30/360 variants in particular have
  enough edge cases that agreement should be verified against known test vectors
  before anyone trusts a P&L number.
- **`PyCurve` costs a GIL acquisition per cashflow per bump.** Prefer the native
  `ZeroCurve` where it fits.

## Requirements

- CMake 3.21+, a C++20 compiler (tested on GCC 11.4)
- Python 3.9+ with `cffi`, `pytest`, `numpy`, `nanobind`, `scikit-build-core`
  for the Python side

