# fast-float-compare

A header-only, branchless-friendly alternative to epsilon-based floating
point equality comparison for C++, based on the **ordered-integer property**
of the IEEE 754 bit layout.

## The idea

In IEEE 754, a `double` or `float` is laid out as `[sign | exponent | mantissa]`,
with the exponent placed *above* the mantissa. For positive numbers, this
means that if you reinterpret the 64 (or 32) bits of a float as a signed
integer, **larger floats map to larger integers** — the bit pattern is
order-preserving.

A direct consequence: two floating point numbers that differ by exactly
**1 unit in the last place (ULP)** — i.e. they are *adjacent representable
values* — have integer representations that differ by exactly **1**.

This gives a comparison that is:

- **Physically motivated**: IEEE 754 guarantees each elementary operation
  (`+`, `-`, `*`, `/`, `sqrt`) is correctly rounded to within 0.5 ULP. Two
  results differing by ≤ 1 ULP represent the same mathematical value up to
  rounding — there is no arbitrary threshold to tune.
- **Scale-invariant by construction**: unlike `fabs(a-b) < eps`, there is no
  separate epsilon to choose for different magnitudes — the integer
  representation already encodes the exponent.
- **Simple**: two `memcpy`s, a subtraction, and a comparison.

```cpp
bool areEqual(double a, double b, int64_t max_ulp = 1) noexcept
{
    int64_t bits1, bits2;
    std::memcpy(&bits1, &a, sizeof(double));
    std::memcpy(&bits2, &b, sizeof(double));

    int64_t diff = bits1 - bits2;
    if (diff < 0) diff = -diff;

    return diff <= max_ulp;
}
```

## Usage

Header-only — just copy `include/fast_compare.hpp` into your project.

```cpp
#include "fast_compare.hpp"

double sum = 0.1 + 0.2;
double ref = 0.3;

fastcmp::areEqual(sum, ref);              // true (1 ULP apart, default max_ulp=1)
fastcmp::areEqual(sum, ref, int64_t{0});  // false (not bit-identical)
fastcmp::areEqualSafe(sum, ref);          // true, also checks for NaN
```

Two entry points are provided:

- `areEqual(a, b, max_ulp = 1)` — fastest path. **Precondition: neither
  argument is NaN.**
- `areEqualSafe(a, b, max_ulp = 1)` — adds `isnan` checks (NaN != NaN, per
  IEEE 754).

### areEqualStrict — fixing the denorm_min false positive

`areEqual` has a documented false positive for opposite-sign values near
`denorm_min()` (see [Known limitation](#known-limitation) below). If this
matters for your application — or you simply want a drop-in replacement
that matches `areEqualRelative`'s behavior in all cases — use
`areEqualStrict` instead. It first compares the sign bits of `a` and `b`:
if they differ, the numbers are equal only when both are zero (so
`+0.0 == -0.0` still holds); otherwise it falls back to the same ULP
comparison as `areEqual`.

```cpp
fastcmp::areEqualStrict(+0.0, -0.0);                    // true
fastcmp::areEqualStrict(denorm_min, -denorm_min);       // false (areEqual gives true here)
fastcmp::areEqualStrictSafe(a, b);                      // + NaN check
```

`areEqualStrict` costs one extra branch and a handful of comparisons. As
shown in [Performance](#performance), it remains faster than
`areEqualRelative` while being correct across the full range of `double`
and `float`, including the sign-crossing edge case.

## Correctness

Verified against `areEqualRelative` (the standard
`fabs(a-b) <= epsilon * max(|a|,|b|)` approach) across:

- Numbers of vastly different magnitude (`1e-311` to `1e300`)
- Positive and negative operands
- Subnormal numbers
- `float` and `double`
- The classic `0.1 + 0.2 != 0.3` case

See [`tests/test_compare.cpp`](tests/test_compare.cpp).

## Performance

On a representative workload (1M random double comparisons, results
differing by 1 ULP, `-O2`):

| Method                            | Relative speed |
|------------------------------------|----------------|
| `areEqual` (this library)          | **1.0x**       |
| `areEqualStrict` (this library)    | ~0.65–0.7x     |
| `areEqualRelative` (classic)       | ~0.5–0.55x     |
| `fabs(a-b) < 1e-9` (naive eps)      | ~1.1–1.2x      |

`areEqual` is consistently **1.8–1.9x faster than `areEqualRelative`**.
`areEqualStrict` adds a sign-bit check to eliminate the
[denorm_min edge case](#known-limitation) below, costing roughly 1.45–1.5x
over `areEqual` — but it is still **~1.25–1.3x faster than
`areEqualRelative`** while matching its behavior in all cases, including
opposite-sign and `+0.0`/`-0.0` comparisons.

Compared to a naive fixed-epsilon check, `areEqual` and `areEqualStrict` are
in the same ballpark in raw speed — but the naive check has no
scale-awareness and silently breaks for very large or very small magnitudes,
whereas the ULP-based threshold has the same meaning at every scale.

Run the benchmark yourself:

```bash
g++ -O2 -std=c++17 -o benchmark benchmark/benchmark.cpp
./benchmark
```

## Known limitation

The signed-integer subtraction can overflow when `a` and `b` have
**opposite signs and both lie extremely close to the smallest subnormal
magnitude** (`std::numeric_limits<T>::denorm_min()`, i.e. around `±5e-324`
for `double`). In that case `bits(a) - bits(b)` exceeds `INT64_MAX` by
exactly 1, wraps around to `INT64_MIN`, and the comparison
`diff <= max_ulp` then evaluates to `true` — i.e. **`areEqual` reports two
numbers of opposite sign as equal**, a false positive.

This is an exceedingly narrow edge case (it only affects values whose
magnitude is within one or two ULPs of `denorm_min()`) that does not arise
in ordinary numerical computation. `areEqual` does not special-case it; if
your application may produce values in that exact regime, validate
separately.

NaN and infinities are handled correctly by `areEqualSafe`. `areEqual` itself
assumes non-NaN input (this is the documented precondition, matching the
common pattern of checking for NaN once, upstream, before a series of
comparisons).

## License

MIT — see [LICENSE](LICENSE).

## Author

Iouri Spiridonov
