# fast-float-compare

Header-only, branchless-friendly utilities for comparing and ordering
IEEE 754 `double`/`float` values via their bit patterns, instead of
epsilon-based heuristics.

Two related but distinct tools:

- **`areEqual`** — "are these the same value, up to 1 ULP of rounding?"
  A fast alternative to `fabs(a-b) <= epsilon * max(|a|,|b|)`.
- **`lessThan` / `compare3`** — a total order over all non-NaN floats,
  including across signs and `+0.0`/`-0.0`, matching `operator<` exactly.

## The idea

In IEEE 754, a `double` or `float` is laid out as `[sign | exponent | mantissa]`,
with the exponent placed *above* the mantissa, and exponent/mantissa bits
laid out identically for positive and negative numbers.

### Equality: areEqual

`areEqual` only ever answers "equal or not equal" — it never needs to
establish an ordering. The relevant property: for two numbers of the
**same sign**, reinterpreting their bits as a signed integer and taking
`|bits1 - bits2|` gives exactly the ULP distance between them. Two numbers
that are *adjacent representable values* (differ by exactly 1 ULP) have
integer representations that differ by exactly **1**, regardless of which
direction "increasing magnitude" points for that sign.

Two numbers of **opposite sign are unequal by definition** — except
`+0.0 == -0.0`. This sign check is not an optional safety net; it is part
of the algorithm. Skipping it breaks `areEqual(x, -x)` for **every nonzero
`x`**, not just an edge case — see [Why the sign check is
mandatory](#why-the-sign-check-is-mandatory).

```cpp
bool areEqual(double a, double b) noexcept
{
    int64_t bits1, bits2;
    std::memcpy(&bits1, &a, sizeof(double));
    std::memcpy(&bits2, &b, sizeof(double));

    bool neg1 = bits1 < 0;
    bool neg2 = bits2 < 0;
    if (neg1 != neg2) return (a == 0) && (b == 0);

    int64_t diff = bits1 - bits2;
    if (diff < 0) diff = -diff;
    return diff <= 1;
}
```

This gives a comparison that is:

- **Physically motivated**: IEEE 754 guarantees each elementary operation
  (`+`, `-`, `*`, `/`, `sqrt`) is correctly rounded to within 0.5 ULP. Two
  results differing by ≤ 1 ULP represent the same mathematical value up to
  rounding — there is no arbitrary threshold to tune.
- **Scale-invariant by construction**: unlike `fabs(a-b) < eps`, there is no
  separate epsilon to choose for different magnitudes — the integer
  representation already encodes the exponent.

The threshold of **1 ULP is fixed and not configurable**. A larger
threshold (2, 4, ...) would just be an arbitrary epsilon in different
units, with the same problem this library is meant to avoid: with a
threshold of 4 ULP, two numbers that are genuinely 4 ULP apart — the
maximum the threshold allows — would be reported as equal, even though
that is the most two numbers can differ while still passing.

> **Note on scope**: `areEqual` compares `double`/`float` *bit patterns* —
> i.e. the values *after* decimal-to-binary rounding has already happened.
> See [Scope: what "equal" means here](#scope-what-equal-means-here) for an
> important caveat about subnormal numbers.

### Ordering: lessThan and compare3

IEEE 754 bit patterns, reinterpreted as **unsigned** integers, are
order-preserving only for non-negative numbers. For negative numbers,
increasing magnitude produces an increasing bit pattern, but increasing
magnitude means a *decreasing* value — so the raw bit pattern is NOT a
total order across signs.

`lessThan` fixes this with the standard branchless "flip" transform
(`to_ordered`, in `detail::`): non-negative numbers get their sign bit set;
negative numbers get all their bits inverted. This maps the full range of
non-NaN floats onto an unsigned integer range that is monotonic with the
float value — including correctly merging `+0.0` and `-0.0` into a single
ordered value (since IEEE 754 `<` treats them as equal).

A direct consequence of the flip: **any positive number compares greater
than any negative number, regardless of magnitude** — `to_ordered` places
the entire non-negative range above the entire negative range as a first
step, before magnitude within each range is considered. This holds even for
extreme, counterintuitive-looking magnitude differences:

```cpp
fastcmp::lessThan(-2.0, -1.0);  // true  (within same sign: ordinary magnitude order)
fastcmp::lessThan(-1.0, 1.0);   // true
fastcmp::lessThan(-0.0, +0.0);  // false (they're equal under <)

fastcmp::compare3(1e-300, -1e300);  // +1  (a tiny positive is still > a huge negative)
fastcmp::compare3(-1.0, 1.0);       // -1  (any negative < any positive)
```

This is the same property [areEqual's mandatory sign check](#why-the-sign-check-is-mandatory)
relies on, applied to ordering instead of equality: sign is checked
(implicitly, via the flip) before magnitude.

`compare3` is a three-way comparison built from `lessThan` and `areEqual`:
returns `-1`, `0`, or `+1`. Note that, consistent with `areEqual`'s
definition of "equal", `compare3` can return `0` for two values that are 1
ULP apart but not bit-identical (same sign only).

## Usage

Header-only — just copy `include/fast_compare.hpp` into your project.

```cpp
#include "fast_compare.hpp"

double sum = 0.1 + 0.2;
double ref = 0.3;

fastcmp::areEqual(sum, ref);              // true (1 ULP apart — adjacent representable values)
fastcmp::areEqualSafe(sum, ref);          // true, also checks for NaN

fastcmp::lessThan(-1.0, 1.0);             // true
fastcmp::lessThan(1e-300, -1e300);        // false (any positive > any negative)
fastcmp::lessThanSafe(a, b);              // + NaN check (NaN has no position -> false)

fastcmp::compare3(1.0, 2.0);              // -1
fastcmp::compare3(-1.0, 1.0);             // -1
fastcmp::compare3(1e-300, -1e300);        // +1 (tiny positive still beats huge negative)
fastcmp::compare3(0.1+0.2, 0.3);          //  0  (1 ULP -> areEqual)
```

All functions have a `*Safe` variant that checks for NaN first and returns
`false` (or, for `compare3`, the `Safe` variants don't exist — `compare3`
is built on the non-Safe primitives; check for NaN yourself before calling
it if needed).

**Preconditions** (all functions): neither argument is NaN, unless using a
`*Safe` variant.

## Why the sign check is mandatory

For ANY nonzero `x`, `areEqual(x, -x)` must be `false`. An implementation
that skips the sign check gets this wrong for **every** nonzero `x`:
`bits(x)` and `bits(-x)` differ only in the sign bit (the top bit). As
signed integers, `bits(x) - bits(-x)` is exactly `±2^(bitwidth-1)`, which
overflows the signed integer type and wraps around to the most negative
representable value (`INT64_MIN` for `double`) — which then passes
`<= 1`.

```cpp
// WITHOUT the sign check (do not do this):
//   areEqual(1.0, -1.0)        -> incorrectly true
//   areEqual(1e300, -1e300)    -> incorrectly true
//   areEqual(denorm_min, -denorm_min) -> incorrectly true
//   ... for literally every nonzero x
```

This is why the sign check is the *first step of the algorithm*, not an
optional extra — see [The idea](#the-idea) above.

## Correctness

Verified against `areEqualRelative` (the standard
`fabs(a-b) <= epsilon * max(|a|,|b|)` approach) and against native
`operator<` across:

- Numbers of vastly different magnitude (`1e-311` to `1e300`)
- Positive and negative operands, including `x` vs `-x` for many `x`
- `+0.0` / `-0.0`
- Subnormal numbers
- `float` and `double`
- The classic `0.1 + 0.2 != 0.3` case
- 200k+ random IEEE 754 bit patterns spanning the full range, including
  subnormals and (skipped) NaNs

See [`tests/test_compare.cpp`](tests/test_compare.cpp).

## Performance

On a representative workload (1M comparisons x 50 repeats, `-O2`):

**Equality** (values ~1 ULP apart, mixed signs):

| Method                      | Relative speed |
|------------------------------|----------------|
| `areEqual` (this library)    | **1.0x**       |
| `areEqualRelative` (classic) | ~0.95–1.05x    |
| `fabs(a-b) < 1e-9` (naive eps)| ~1.4–1.5x      |

`areEqual` is roughly on par with `areEqualRelative` — slightly faster in
most runs. This is the honest cost of doing the sign check correctly: an
earlier version of this library skipped it and measured faster, but was
incorrect for every `x` vs `-x` pair (see [Why the sign check is
mandatory](#why-the-sign-check-is-mandatory)). `areEqual` remains
preferable to `areEqualRelative` because its threshold has a fixed,
physically-motivated meaning at every scale, whereas
`areEqualRelative`'s epsilon must be chosen by the caller.

**Ordering** (independent random values, mixed signs):

| Method                          | Relative speed |
|-----------------------------------|----------------|
| `operator<` (native)              | **1.0x**       |
| `lessThan` (this library)         | ~2.0–2.5x      |
| three-way via `< `/`>` (native)    | ~1.0x (baseline for compare3) |
| `compare3` (this library)         | ~1.2–1.3x      |

`lessThan` costs roughly 2–2.5x a native `<` (a single `comisd`
instruction vs. ~20 integer instructions, fully branchless — verified in
generated assembly). `compare3` is closer to native, at ~1.2–1.3x, because
`areEqual`'s 1-ULP tolerance occasionally short-circuits to "equal" where a
strict `<`/`>` chain would need both comparisons.

Use `lessThan`/`compare3` when you need a **total order** that handles
sign-crossing and `±0.0` correctly in generic/templated code (e.g. as a
comparator for `std::sort` over values that may include negative numbers
and zeros and must match `operator<` semantics exactly) — not as a
general replacement for `operator<` on values you already know are
same-signed.

Run the benchmark yourself:

```bash
g++ -O2 -std=c++17 -o benchmark benchmark/benchmark.cpp
./benchmark
```

## Scope: what "equal" means here

`areEqual` answers a precise question: **are these two `double` (or `float`)
values bit-identical, or adjacent representable values (same sign)?** It
does *not* — and cannot — answer a different question that is easy to
conflate with it: *are the decimal numbers I wrote in my source code the
same?*

Every decimal literal in a C++ program is rounded to the nearest
representable `double` at compile time (the "Level 1 → Level 2" conversion
in IEEE 754's own terminology). For most numbers this rounding is invisible.
But in the subnormal range, representable `double` values become extremely
sparse — the gap between adjacent subnormals (`denorm_min() ≈ 4.94e-324`) is
enormous *relative to* the numbers themselves. Two decimal literals that
differ by many significant digits can land on **the same** `double`:

```cpp
double x1 = 1.234567890987650e-311;
double x2 = 1.23456789098787441868238613483e-311;

// x1 and x2 differ starting at the 9th significant digit —
// a relative difference of roughly 1e-15.
//
// But both round to the SAME double:
//   bits(x1) == bits(x2)
//   areEqual(x1, x2) == true
```

This is a fundamental property of `double` as a storage format: in the
subnormal region, the decimal-to-binary rounding step can be lossy enough
to map distinguishable decimal inputs onto an identical bit pattern, and no
comparison performed *after* that rounding — `areEqual`, `areEqualRelative`,
ULP-based or epsilon-based — can recover the information that was already
lost.

If your application's correctness depends on distinguishing decimal inputs
at that scale, the comparison needs to happen on the decimal values
themselves, before they are converted to `double`.

## License

MIT — see [LICENSE](LICENSE).

## Author

Iouri Spiridonov
