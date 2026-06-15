# fast-float-compare

Header-only, branchless-friendly utilities for comparing and ordering
IEEE 754 `double`/`float` values via their bit patterns, instead of
epsilon-based heuristics.

Two related but distinct tools:

- **`areEqual`** — "are these the same value, up to 1 ULP of rounding?"
  A fast alternative to `fabs(a-b) <= epsilon * max(|a|,|b|)`.
- **`lessThan` / `compare3`** — a total order over all non-NaN floats,
  including across signs and `+0.0`/`-0.0`, following IEEE 754's
  `totalOrder` predicate.

Both tools share the same underlying view: a float's sign always takes
priority over its magnitude. `+0.0` and `-0.0` are treated as distinct,
infinitesimally-signed values rather than as "the number zero with no
sign" — consistent with IEEE 754's `totalOrder`, where `-0.0 < +0.0`
strictly.

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

Two numbers of **opposite sign are unequal, always** — including
`+0.0` vs `-0.0`. This sign check is not an optional safety net; it is part
of the algorithm, for two reasons:

1. **Correctness for nonzero values**: skipping it breaks `areEqual(x, -x)`
   for **every nonzero `x`**, not just an edge case — see [Why the sign
   check is mandatory](#why-the-sign-check-is-mandatory).
2. **Consistency with `lessThan`/`compare3`**: `lessThan(-0.0,+0.0)` is
   `true` (`-0.0 < +0.0` under `totalOrder`). If `areEqual(-0.0,+0.0)` were
   also `true`, `compare3` would return `0` for a pair it also reports as
   `a < b` — a three-way comparison cannot do both.

```cpp
bool areEqual(double a, double b) noexcept
{
    int64_t bits1, bits2;
    std::memcpy(&bits1, &a, sizeof(double));
    std::memcpy(&bits2, &b, sizeof(double));

    bool neg1 = bits1 < 0;
    bool neg2 = bits2 < 0;
    if (neg1 != neg2) return false;  // different signs: never equal

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
float value.

A direct consequence of the flip: **any positive number compares greater
than any negative number, regardless of magnitude** — `to_ordered` places
the entire non-negative range above the entire negative range as a first
step, before magnitude within each range is considered. This holds even for
extreme, counterintuitive-looking magnitude differences:

```cpp
fastcmp::lessThan(-2.0, -1.0);  // true  (within same sign: ordinary magnitude order)
fastcmp::lessThan(-1.0, 1.0);   // true
fastcmp::lessThan(-0.0, +0.0);  // true  (-0.0 < +0.0 under totalOrder)

fastcmp::compare3(1e-300, -1e300);  // +1  (a tiny positive is still > a huge negative)
fastcmp::compare3(-1.0, 1.0);       // -1  (any negative < any positive)
fastcmp::compare3(-0.0, +0.0);      // -1  (-0.0 < +0.0; see areEqual above)
```

Because `+0.0` and `-0.0` differ only in their sign bit — the same bit that
orders every other positive/negative pair — no special case is needed for
them: "any positive orders above any negative" already implies
`+0.0 > -0.0`, treating both zeros as infinitesimals of their respective
sign. This is the same property [areEqual's mandatory sign
check](#why-the-sign-check-is-mandatory) relies on, applied to ordering
instead of equality: sign is checked (implicitly, via the flip / via the
`neg1 != neg2` branch) before magnitude.

**The one place `lessThan` differs from `operator<`:** IEEE 754's `<`
treats `+0.0` and `-0.0` as equal (`-0.0 < 0.0` is `false`), but
`lessThan(-0.0, +0.0)` is `true`. This is intentional — see above — and is
the only pair of non-NaN values where `lessThan(a,b) != (a < b)`.

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

fastcmp::lessThan(-1.0, 1.0);             // true
fastcmp::lessThan(1e-300, -1e300);        // false (any positive > any negative)
fastcmp::lessThan(-0.0, +0.0);            // true  (-0.0 < +0.0 under totalOrder)

fastcmp::compare3(1.0, 2.0);              // -1
fastcmp::compare3(-1.0, 1.0);             // -1
fastcmp::compare3(1e-300, -1e300);        // +1 (tiny positive still beats huge negative)
fastcmp::compare3(0.1+0.2, 0.3);          //  0  (1 ULP -> areEqual)
fastcmp::compare3(-0.0, +0.0);            // -1
```

**Precondition** (all functions): neither argument is NaN. None of these
functions check for NaN at runtime — callers are expected to ensure inputs
are valid numbers before calling, consistent with this library's focus on
the comparison itself rather than input validation.

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
optional extra — see [The idea](#the-idea) above. The check is a single
`if (neg1 != neg2) return false;` — cheaper than the special-cased
`+0.0 == -0.0` check an earlier version of this library had, and (as
explained above) more correct too.

## Correctness

Verified against `areEqualRelative` (the standard
`fabs(a-b) <= epsilon * max(|a|,|b|)` approach) and against native
`operator<` across:

- Numbers of vastly different magnitude (`1e-311` to `1e300`)
- Positive and negative operands, including `x` vs `-x` for many `x`
- `+0.0` / `-0.0`, including their (intentional) divergence from `<`
- Subnormal numbers
- `float` and `double`
- The classic `0.1 + 0.2 != 0.3` case
- 200k+ random IEEE 754 bit patterns spanning the full range, including
  subnormals and (skipped) NaNs

See [`tests/test_compare.cpp`](tests/test_compare.cpp).

## Performance

Measured in **CPU cycles per call** via `__rdtsc` with `lfence`
serialization, taking the median of 50 trials over 100k elements each
(median-of-medians is far more stable than wall-clock timing in a
shared/virtualized environment, where wall-clock measurements of these
functions can vary 2-3x between runs purely from competing processes).
`-O2 -march=native`.

**Equality** (values ~1 ULP apart, mixed signs):

| Method                      | Cycles/call | Relative to areEqual |
|------------------------------|-------------|------------------------|
| `areEqual` (this library)    | **~1.95**   | 1.0x                   |
| `areEqualRelative` (classic) | ~2.27       | ~1.16x                 |
| `fabs(a-b) < 1e-9` (naive eps)| ~1.48       | ~0.76x                 |

`areEqual` is consistently **~15-20% faster than `areEqualRelative`**, on
top of having a fixed, physically-motivated threshold instead of a
caller-chosen epsilon. The naive fixed-epsilon check is faster still in raw
cycles, but doesn't scale: it silently breaks for magnitudes where `1e-9`
isn't an appropriate absolute threshold.

**Ordering** (independent random values, mixed signs):

| Method                          | Cycles/call | Relative to native |
|-----------------------------------|-------------|----------------------|
| `operator<` (native)              | **~1.50**   | 1.0x                 |
| `lessThan` (this library)         | ~1.95       | ~1.3x                |
| three-way via `<`/`>` (native)     | ~11.8       | 1.0x (baseline for compare3) |
| `compare3` (this library)          | ~11.8       | ~1.0x                |

`lessThan` costs about 0.4-0.5 cycles over a native `<` (a single `comisd`
instruction vs. ~13 integer instructions, fully branchless — verified in
generated assembly: `sar`/`or`/`xor` ×2 plus a final `cmp`/`setb`, zero
jumps). `compare3` is essentially free relative to a native `<`/`>`
three-way chain — both cost ~11.8 cycles, dominated by the inherent cost of
producing a three-valued result rather than by either implementation.

Use `lessThan`/`compare3` when you need a **total order** that handles
sign-crossing correctly in generic/templated code (e.g. as a comparator for
`std::sort`) and where the `-0.0`/`+0.0` divergence from `<` (see
[Ordering](#ordering-lessthan-and-compare3) above) is acceptable or
desired — not as a general drop-in replacement for `operator<` on values
you already know are same-signed and where `±0.0` must compare equal.

Run the benchmark yourself (x86/x64 only, uses `__rdtsc`):

```bash
g++ -O2 -std=c++17 -march=native -o benchmark benchmark/benchmark.cpp
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
