# fast-float-compare

A header-only, branchless alternative to epsilon-based floating point
equality comparison for C++, based on the ordered-integer property of the
IEEE 754 bit layout.

## The idea

In IEEE 754, a `double` or `float` is laid out in memory as
`[sign | exponent | mantissa]` — for `double`, 1 sign bit, 11 exponent
bits, and 52 mantissa bits, all packed into the same 64 bits that also
form a 64-bit integer. This means the *same* 64 bits in memory can be read
either as a `double` or as an `int64_t`; reading them as the other type
without changing them is sometimes called **type punning** (or bit
reinterpretation). In C++ this is done safely with `std::memcpy`:

```cpp
double a = 1.0;
int64_t bits1;
std::memcpy(&bits1, &a, sizeof(double));  // bits1 now holds the same 64 bits as a,
                                            // just read as a signed integer
```

`bits1` is simply the name this document uses for "the bits of `a`,
reinterpreted as `int64_t`" — likewise `bits2` for a second value `b`.
For `a = 1.0`, `bits1` works out to `4607182418800017408`; for
`a = 1.0000000000000002` (the very next representable `double` above
`1.0`), the same process gives `4607182418800017409` — exactly one more.
This is not a coincidence: it is the key property this library relies on.

**For two numbers of the same sign**, `bits1` and `bits2` (computed this
way) increase or decrease together with the magnitude of the underlying
floats, one-for-one. So taking `|bits1 - bits2|` gives exactly the number
of representable `double` values that lie between `a` and `b` — their
**ULP distance** ("ULP" = unit in the last place). Two numbers that are
*adjacent representable values* (ULP distance of exactly 1) therefore have
`bits1` and `bits2` differing by exactly **1**, as in the `1.0` example
above.

Two numbers of **opposite sign are unequal, always** — including `+0.0` vs
`-0.0`. This sign check is not an optional safety net; it is part of the
algorithm. Skipping it breaks `areEqual(x, -x)` for **every nonzero `x`**:
`bits1` and `bits2` for `x` and `-x` differ only in the sign bit, so their
difference is exactly `±2^63`, which overflows the signed 64-bit integer
type and wraps around to a value that incorrectly passes `<= 1`.

Putting this together:

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

## Usage

Header-only — just copy `include/fast_compare.hpp` into your project.

```cpp
#include "fast_compare.hpp"

double sum = 0.1 + 0.2;
double ref = 0.3;

fastcmp::areEqual(sum, ref);   // true (differ by at most 1 ULP — adjacent representable values)
fastcmp::areEqual(1.0, -1.0);  // false (different signs)
fastcmp::areEqual(+0.0, -0.0); // false (different signs, no exception for zero)
```

**Precondition**: neither argument is NaN. `areEqual` does not check for
NaN at runtime — callers are expected to ensure inputs are valid numbers
before calling, consistent with this library's focus on the comparison
itself rather than input validation.

## Correctness

Verified against `areEqualRelative` (the standard
`fabs(a-b) <= epsilon * max(|a|,|b|)` approach) across:

- Numbers of vastly different magnitude (`1e-311` to `1e300`)
- Positive and negative operands
- `+0.0` / `-0.0`
- Subnormal numbers
- `float` and `double`
- The classic `0.1 + 0.2 != 0.3` case

See [`tests/test_compare.cpp`](tests/test_compare.cpp).

## Building the tests

`tests/test_compare.cpp` includes the header via a relative path:

```cpp
#include "../include/fast_compare.hpp"
```

This path is resolved relative to the location of `test_compare.cpp`
itself, not your terminal's current directory — so the include works as
long as the repository's folder structure (`include/` and `tests/` as
siblings) is preserved, regardless of which directory you run `g++` from:

```bash
# From inside the fast-float-compare/ folder (repo root):
g++ -O2 -std=c++17 -o test_compare tests/test_compare.cpp
./test_compare
```

**If you've copied just the `.cpp` and `.hpp` files into a single folder**
(for example, into an existing IDE project, or a fresh folder without the
`include/`/`tests/` structure), the `../include/` path will not resolve.
Either:

- Recreate the `include/` + `tests/` folder structure, or
- Edit the first `#include` line in your copy of `test_compare.cpp` to:
  ```cpp
  #include "fast_compare.hpp"
  ```
  and place `fast_compare.hpp` in the same folder as `test_compare.cpp`.

The same applies to `benchmark/benchmark.cpp`, which includes the header
the same way (`#include "../include/fast_compare.hpp"`).

## Performance

Measured in **CPU cycles per call** via `__rdtsc` with `lfence`
serialization, taking the median of 50 trials over 100k elements
(median-of-medians is far more stable than wall-clock timing in a
shared/virtualized environment, where wall-clock measurements of these
functions can vary 2-3x between runs purely from competing processes).
`-O2 -march=native`.

| Method                      | Cycles/call |
|------------------------------|-------------|
| `areEqual` (this library)    | ~3.3        |
| `areEqualRelative` (classic) | ~3.3        |

In practice the two cost about the same — `areEqual` is marginally faster
(within ~1%) in repeated runs. The case for `areEqual` over
`areEqualRelative` rests primarily on its fixed, physically-motivated
threshold (1 ULP, with a documented reason that threshold and no other is
correct) rather than on raw speed: with `areEqualRelative`, the caller
must choose an epsilon, and that choice is itself a source of bugs (too
tight and rounding noise causes false negatives; too loose and the
comparison silently accepts larger errors than intended).

Run the benchmark yourself (x86/x64 only, uses `__rdtsc`):

```bash
g++ -O2 -std=c++17 -march=native -o benchmark benchmark/benchmark.cpp
./benchmark
```

## Scope: what "equal" means here

`areEqual` answers a precise question: **are these two `double` (or `float`)
values bit-identical, or adjacent representable values (same sign)?** It
does *not* — and cannot — answer two different questions that are easy to
conflate with it.

**It does not tell you whether the original decimal numbers were the
same.** Every decimal literal in a C++ program is rounded to the nearest
representable `double` at compile time. For most numbers this rounding is
invisible, but in the subnormal range, representable `double` values
become extremely sparse — the gap between adjacent subnormals
(`denorm_min() ≈ 4.94e-324`) is enormous *relative to* the numbers
themselves. Two decimal literals that differ by many significant digits
can land on **the same** `double`:

```cpp
double x1 = 1.234567890987650e-311;
double x2 = 1.23456789098787441868238613483e-311;

// x1 and x2 differ starting at the 9th significant digit, but both
// round to the SAME double at compile time:
//   bits(x1) == bits(x2)
//   areEqual(x1, x2) == true
```

This is a fundamental property of `double` as a storage format, not a bug:
the decimal-to-binary rounding step can be lossy enough to map
distinguishable decimal inputs onto an identical bit pattern, and no
comparison performed *after* that rounding — `areEqual`, `areEqualRelative`,
or anything else operating on the resulting `double` — can recover
information already lost at that step.

**If you need to compare decimal values exactly** (not their `double`/
`float` approximations), the right tool is an arbitrary-precision decimal
type — e.g. Python's `decimal.Decimal`, Java's `BigDecimal`, or a C++
equivalent — not `areEqual` or any other binary-floating-point comparison.
Those types avoid the problem entirely by never converting to a binary
approximation in the first place; `areEqual` (and `==`, and
`areEqualRelative`) operate strictly on `double`/`float` values that have
already been rounded, and answer only "are these two roundings the same,
up to 1 ULP" — not "were the original decimal inputs the same".

## License

MIT — see [LICENSE](LICENSE).

## Author

Iouri Spiridonov
