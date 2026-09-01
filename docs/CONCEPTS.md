# The finance behind libfirisk

Written for a developer who is comfortable with the code but not necessarily
with fixed income. Every formula here corresponds to something in `src/core/`,
and the file is named where it helps.

---

## 1. A bond is a schedule of cashflows

A fixed rate bullet bond is a promise to pay a fixed coupon at regular
intervals and return the face value at maturity. Everything else in this library
is a function of that cashflow schedule.

A 4.5% semiannual bond on 100 face, issued February 2024, maturing February
2034, pays 2.25 every six months and 102.25 at the end.

Two things make this less trivial than it sounds.

**The payment dates have to be generated.** `src/core/time/schedule.cpp` works
*backwards* from maturity, stepping one coupon period at a time. Backwards is
deliberate: the maturity date is the fixed, contractual anchor, and any
irregularity should land at the front where a short or long first coupon is
normal and expected. Generating forward from issue would push the irregularity
to the back, producing a stub at maturity — which almost never matches how the
instrument was actually documented.

A period that isn't a full frequency step is a **stub**. It arises when the
issue date doesn't sit on the regular grid, or when the deal specifies a first
coupon date that doesn't. `Schedule::build` flags each period with `is_stub`,
which matters for ACT/ACT ICMA below.

**Payment dates get adjusted, accrual dates don't.** If a coupon date lands on a
weekend or holiday, the money moves to the next business day — but the interest
still accrues to the original unadjusted date. That's the standard bond
treatment, and `Period` in `schedule.hpp` keeps them as separate fields for
exactly this reason. Swaps do it differently: they adjust both. Getting this
wrong changes every accrued interest number by a day or two.

---

## 2. Day counts: how much of a year is this?

A coupon is quoted as an annual rate. To pay it over a period you need the
period's length as a fraction of a year, and there is no single right answer —
there are conventions, and the convention is part of the instrument's terms.

`src/core/time/daycount.cpp` implements six.

**ACT/360** — actual days elapsed, divided by 360. A 182-day period is
182/360 = 0.5056 years. Because the denominator is smaller than a real year,
this systematically pays *more* than the quoted rate suggests. Standard for
money markets and most floating rate legs.

**ACT/365F** — actual days over a fixed 365. Closer to honest. Used for GBP
instruments and, in this library, for measuring time to a cashflow when
discounting off a curve.

**ACT/ACT ISDA** — actual days, but the denominator is each calendar year's own
length. A period spanning a year boundary is split: the part in 2027 divides by
365, the part in 2028 by 366. This is the only convention that treats leap years
correctly, and it's why the implementation has a loop rather than a formula.

**ACT/ACT ICMA** — the odd one out. The denominator is the actual length of the
coupon period the accrual sits in, times the frequency. A regular semiannual
period is therefore *exactly* 0.5 years by construction, regardless of whether
it contained 181 or 184 days. This makes every regular coupon identical, which
is what bond markets want. It's also why `year_fraction` takes an `AccrualPeriod`
argument that every other convention ignores: ICMA needs to know which coupon
period it's inside.

For a **stub**, ICMA measures against the notional regular period the stub sits
within, constructed by stepping one frequency backward from the period end. A
long stub correctly produces a year fraction greater than 1/frequency.

**30/360 Bond** and **30E/360** — pretend every month has 30 days and every year
360. Both give exactly 0.5 for a semiannual period. They differ in one detail:
30/360 Bond only pulls the second date's day back from 31 to 30 if the first
date's day was already 30 or 31; 30E/360 clamps both unconditionally. That one
rule is the source of a great many one-day discrepancies between systems.

The library refuses to guess. Asking for ICMA without supplying a valid
`AccrualPeriod` returns `FIR_E_BAD_SCHEDULE` rather than quietly falling back to
ISDA, because silently producing a plausible-looking wrong number is worse than
an error.

---

## 3. Clean, dirty, and accrued

Between coupon dates the holder has earned interest that hasn't been paid yet.
That's **accrued interest**:

```
accrued = face × coupon_rate × yearfrac(period_start, today)
```

The **dirty price** (or full price, or invoice price) is the present value of all
remaining cashflows. It's what you actually pay.

The **clean price** is the dirty price minus accrued. It's what gets quoted.

```
clean = dirty − accrued
```

Why quote the clean price? Because the dirty price sawtooths: it climbs steadily
as accrual builds, then drops by the coupon amount on payment day. That
discontinuity is pure mechanics and tells you nothing about whether the bond got
cheaper. Stripping accrued out leaves a number that moves only when the market
moves.

`PriceComponents` in `src/core/pricing/price.hpp` carries all three, and
`Bond::accrued` returns zero if the valuation date isn't inside a coupon period.

---

## 4. Present value and yield to maturity

### Discounting

A dollar in five years is worth less than a dollar today. A **discount factor**
`df(t)` converts a future amount to present value, and the price is the sum:

```
P = Σ CF_t × df(t)
```

How `df(t)` is built is the only interesting question. Two routes.

### Route one: a single yield

Assume one flat rate applies to every cashflow. That rate is the **yield to
maturity** — the single discount rate that makes the PV equal the observed
price. Three ways to compound it (`df_derivs` in `price.cpp`):

| Compounding | Discount factor | Where it's used |
|---|---|---|
| Simple | `1 / (1 + y·t)` | Money markets, sub-year |
| Periodic | `(1 + y/k)^(−k·t)` | Bond markets, k = coupon frequency |
| Continuous | `exp(−y·t)` | Curves, derivatives, most quant work |

Periodic is what a bond trader means by "yield." The `k` is the coupon
frequency, so a semiannual bond's 5% yield means 2.5% twice a year, which
compounds to 5.0625% effective. This is why quoted yields aren't directly
comparable across frequencies without converting.

The important structural point: **price is monotone decreasing in yield**. Higher
yield, lower price, always. That's what lets a bracketing solver work.

### Solving for yield

You observe a price and want the yield. There's no closed form — it's a
polynomial root — so `yield_from_price` uses Newton's method with a maintained
bracket (`solve_newton` in `src/core/pricing/solve.cpp`).

Plain Newton converges quadratically but can shoot off to a nonsensical root on
a deeply discounted long bond. The implementation takes the Newton step only when
it lands inside the current bracket and makes progress, and bisects otherwise.
That keeps quadratic convergence in the common case and guarantees termination in
the pathological one.

The bracket's lower bound is convention-dependent, which is subtler than it
looks. Periodic compounding goes singular at `y = −k`. Simple compounding goes
singular at `y = −1/t`, and `t` differs per cashflow — for a ten year bond that's
around −0.13, far closer to zero than the −0.99 that suits continuous. Using one
bound for all three leaves the objective non-monotone over part of the range, and
a bracket test over a non-monotone function means nothing.

### Route two: a curve

A single yield is a fiction. Real markets have a **term structure**: two-year
money and ten-year money cost different amounts. A discount curve gives a
different `df(t)` for each maturity.

`libfirisk` doesn't build curves. It takes one as a callback
(`fir_curve_t.discount`), so a bootstrapped SOFR curve, a flat rate, or anything
else works identically. `price_from_curve` in `price.cpp` is just the sum above
with the caller's factors.

---

## 5. Z-spread

Price a bond off a risk-free curve and you'll usually get a number higher than
what it actually trades at. The difference is credit, liquidity, and whatever
else the market is charging.

The **z-spread** (zero-volatility spread) is the constant spread that, added to
every point on the curve, reproduces the observed price:

```
P_observed = Σ CF_t × df(t) × exp(−z·t)
```

Solve for `z`. `zspread_from_price` brackets it and hands off to Brent's
method — derivative-free, because writing the analytic derivative through an
arbitrary caller-supplied curve isn't worth it.

Unlike a nominal spread over a single benchmark bond, the z-spread accounts for
the whole shape of the curve, which makes it comparable across instruments with
different maturities and coupons.

This library applies the spread **continuously**, which keeps it independent of
the bond's coupon frequency. Some systems apply it periodically at the bond's
own frequency; those numbers won't match.

---

## 6. Duration

Duration answers: if yields move, how much does the price move?

### Macaulay duration

The original definition, and the one on the whiteboard:

```
                Σ t · CF_t / (1+y)^t
    D_mac  =   ──────────────────────
                        P
```

This is the **present-value-weighted average time to receive the cashflows**,
measured in years. It's a centre of mass: each cashflow's arrival time weighted
by how much of today's price it accounts for.

Two immediate consequences:

- A **zero coupon bond's Macaulay duration equals its time to maturity**, since
  there's only one cashflow. This is the cleanest test in the suite.
- A **coupon bond's duration is always less than its maturity**, because the
  coupons pull the average forward. Higher coupons pull harder, so a 9% bond has
  shorter duration than a 1% bond of the same maturity.

In the code this is `twpv / pv` — `pv_derivatives` accumulates the
time-weighted sum in the same pass that computes the price.

### Modified duration

Macaulay duration is a time, which is intuitive but not directly a sensitivity.
**Modified duration** is:

```
    D_mod = −(1/P) · dP/dy
```

the percentage price change per unit change in yield. For periodic compounding
these are related exactly:

```
                D_mac
    D_mod  =  ─────────
              (1 + y/k)
```

which is the second formula on the whiteboard. The `(1 + y/k)` divisor comes out
of differentiating the periodic discount factor.

`libfirisk` computes `D_mod` from `dP/dy` directly rather than via the identity.
The identity is verified in the tests, but computing from the derivative means
continuous compounding — where the divisor is 1 and `D_mod = D_mac` — falls out
with no special case.

The practical use:

```
    ΔP/P  ≈  −D_mod · Δy
```

A bond with `D_mod` of 7 loses about 0.7% of its value on a 10bp yield rise.

---

## 7. Convexity

Modified duration is a first derivative, so it's a straight-line approximation to
a curved relationship. **Convexity** is the second-order term:

```
    C = (1/P) · d²P/dy²
```

and the improved approximation is:

```
    ΔP/P  ≈  −D_mod · Δy  +  ½ · C · (Δy)²
```

For a vanilla bond convexity is **positive**, which is good news for the holder:
prices rise more on a yield fall than they drop on an equal yield rise. The
price-yield curve bends away from the straight line in the holder's favour.

Positive convexity is why duration alone understates a bond's value in volatile
markets, and why the difference between an analytic derivative and a one-sided
finite difference is exactly the convexity term. That distinction shows up
directly in the test suite: `dv01` matched against a one-sided reprice fails at
tight tolerance, and matches to six digits against a central difference.

Longer and lower-coupon bonds have more convexity, because cashflows are spread
further out in time.

---

## 8. DV01

Traders don't think in percentages, they think in money. **DV01** (dollar value
of an 01, also PV01 or BPV) is the price change for a one basis point yield move:

```
    DV01  =  −dP/dy × 0.0001  =  D_mod × P_dirty × 0.0001
```

Note the **dirty** price. DV01 is a cash sensitivity, and cash includes accrued.

Why this matters more than duration in practice: DV01 is additive across a
portfolio. Two positions with DV01 of 4,000 and −1,500 net to 2,500 of exposure,
which is what a hedge is sized against. Durations can't be added like that — they
have to be weighted by market value first.

`libfirisk` returns DV01 in the same units as the price. A face of 100 gives DV01
per 100 of notional; scale by position size.

---

## 9. Curve risk: effective duration

Everything above assumes a single yield moving. Real risk comes from the *curve*
moving, and the two aren't the same.

**Effective duration** shifts the whole curve up and down by a small amount,
reprices both times, and takes a central difference:

```
                     P(−Δy) − P(+Δy)
    D_eff  =  ───────────────────────────
                    2 · P · Δy
```

`curve_risk` in `src/core/risk/curve_risk.cpp` does exactly this, using the
context's configured bump size. Central rather than one-sided because the
one-sided error is first-order in the bump while the central error is second
order — the same convexity argument as above.

For a vanilla bond off a flat curve, effective duration and modified duration
agree closely, and the test suite pins that. They diverge when the instrument has
optionality (callables, mortgages) where cashflows themselves change as rates
move — at which point effective duration is the only meaningful one, since there
is no single yield to differentiate against.

**Effective convexity** is the same idea one order up:

```
                  P(+Δy) + P(−Δy) − 2P
    C_eff  =  ──────────────────────────
                      P · (Δy)²
```

### Bump size matters

The bump is a numerical-differentiation step and it has the usual tension.
Too large and truncation error dominates — you're measuring the chord, not the
tangent, and picking up convexity you didn't ask for. Too small and floating
point cancellation dominates — you're subtracting two nearly-equal numbers and
keeping the noise.

The library restricts bumps to [0.01, 1000] basis points. Below 0.01bp, double
precision cancellation costs more than truncation error saves. 1bp is the default
and a sensible one.

### Restoring the curve

Every bumped revaluation is wrapped in a `BumpGuard` whose destructor calls
`reset`. A bump that survived a failed reprice would leave the caller's curve
permanently shifted, silently corrupting every subsequent price with no way to
detect it. The tests verify the curve level is bit-identical after risk calls.

---

## 10. Key rate durations

A parallel curve shift is also a fiction. Curves twist and steepen; the two-year
point moves without the ten-year point following.

**Key rate durations** decompose the total sensitivity by maturity bucket. Bump
one pillar at a time, hold the others fixed, reprice:

```
                        P(−Δy at T) − P(+Δy at T)
    KRD(T)  =  ──────────────────────────────────────
                          2 · P · Δy
```

A ten year bond's key rate profile has most of its weight at the ten year point
(that's where the principal lands) with smaller contributions across the coupon
dates. A barbell of two-year and thirty-year bonds can have the same total
duration as a ten-year bullet while having a completely different key rate
profile — and it will behave completely differently when the curve twists.

**The key rates should approximately sum to the effective duration**, since
bumping every pillar together is a parallel shift. "Approximately" because it
depends entirely on how the caller's curve decays a pillar bump toward its
neighbours.

This is the part `libfirisk` deliberately does not own. The library asks the
curve to bump a tenor; the interpolation is the curve's business. A curve that
moves its whole level on every pillar bump will produce key rates that sum to N
times the parallel figure. The bundled `ZeroCurve` in the Python bindings
interpolates linearly between pillars, so a bump at 5y decays to zero at 3y and
7y, and the sum reconstructs the parallel shift properly.

---

## 11. Putting it together

For the 4.5% semiannual bond maturing February 2034, valued August 2026, at a 5%
periodic yield:

| Measure | Roughly | Reading |
|---|---|---|
| Clean price | 96.7 | Below par because the yield exceeds the coupon |
| Accrued | ~0.2 | Interest earned since the last coupon |
| Macaulay duration | 6.2y | PV-weighted average time to the cashflows |
| Modified duration | 6.1 | Price falls ~6.1% per 1% yield rise |
| Convexity | 44 | The correction that makes that estimate better |
| DV01 | 0.059 | Price moves ~0.06 per basis point, per 100 face |

Run `./build.sh examples` for the actual numbers from your build.

---

## Further reading

- Fabozzi, *Bond Markets, Analysis, and Strategies* — the standard reference for
  everything in this document
- Tuckman & Serrat, *Fixed Income Securities* — better on curve construction and
  the term structure
- ISDA 2006 Definitions, section 4.16 — the authoritative source for the day
  count conventions implemented here
