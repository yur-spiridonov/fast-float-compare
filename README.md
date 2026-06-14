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

| Method                          | Relative speed |
|----------------------------------|----------------|
| `areEqual` (this library)        | **1.0x**       |
| `areEqualRelative` (classic)     | ~0.6–0.7x      |
| `fabs(a-b) < 1e-9` (naive eps)    | ~0.9–1.0x      |

`areEqual` is consistently **1.4–1.6x faster than `areEqualRelative`**, while
giving identical results and avoiding the choice of an arbitrary epsilon.
Compared to a naive fixed-epsilon check it is roughly on par in raw speed —
but the naive check has no scale-awareness and silently breaks for very
large or very small magnitudes, whereas `areEqual`'s threshold (in ULPs) has
the same meaning at every scale.

Run the benchmark yourself:

```bash
g++ -O2 -std=c++17 -o benchmark benchmark/benchmark.cpp
./benchmark
```

## Known limitation

The signed-integer reinterpretation can in principle overflow when `a` and
`b` have **opposite signs and both lie extremely close to the smallest
subnormal magnitude** (`std::numeric_limits<T>::denorm_min()`, i.e. around
`5e-324` for `double`). This is an exceedingly narrow edge case that does
not arise in ordinary numerical computation. `areEqual` does not special-case
it; if your application may produce values in that exact regime, validate
separately.

NaN and infinities are handled correctly by `areEqualSafe`. `areEqual` itself
assumes non-NaN input (this is the documented precondition, matching the
common pattern of checking for NaN once, upstream, before a series of
comparisons).

## License

MIT — see [LICENSE](LICENSE).

## Author

Iouri Spiridonov
