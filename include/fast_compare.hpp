// fast_compare.hpp
// A fast, branchless alternative to epsilon-based floating point equality
// comparison, based on the ordered-integer property of the IEEE 754 bit
// layout.
//
// SPDX-License-Identifier: MIT
// Author: Iouri Spiridonov

#pragma once

#include <cstdint>
#include <cstring>

namespace fastcmp {

namespace detail {

// ulp_equal_same_sign: the core ULP-distance check, valid ONLY when bits1
// and bits2 have the same sign bit (both >= 0 or both < 0 as signed
// integers). Used internally by areEqual after the sign check. Not
// exposed directly, because calling it on opposite-sign inputs is unsafe
// (see areEqual's documentation for why).
template <typename Int>
inline bool ulp_equal_same_sign(Int bits1, Int bits2) noexcept
{
    Int diff = bits1 - bits2;
    if (diff < 0) diff = -diff;
    return diff <= 1;
}

template <typename Float, typename Int>
inline bool ulp_equal(Float a, Float b) noexcept
{
    Int bits1, bits2;
    std::memcpy(&bits1, &a, sizeof(Float));
    std::memcpy(&bits2, &b, sizeof(Float));

    // Sign bit is the top bit: negative when interpreted as signed Int.
    bool neg1 = bits1 < 0;
    bool neg2 = bits2 < 0;

    if (neg1 != neg2)
    {
        // Different signs: NEVER equal, including +0.0 vs -0.0.
        //
        // This branch is NOT optional: without it, bits1 - bits2 for x
        // and -x of equal magnitude is exactly +-2^(bitwidth-1), which
        // overflows the signed integer and wraps to the most negative
        // representable value -- which then passes "<= 1". areEqual(x,-x)
        // would incorrectly return true for EVERY nonzero x, not just
        // values near denorm_min().
        return false;
    }

    return ulp_equal_same_sign<Int>(bits1, bits2);
}

} // namespace detail

// ---------------------------------------------------------------------
// areEqual
//
// Compares two floating point numbers by reinterpreting their IEEE 754
// bit patterns as integers and checking whether they are bit-identical or
// adjacent representable values (ULP distance <= 1).
//
// THE ALGORITHM, IN TWO STEPS:
//   1. Different signs -> unequal. ALWAYS, including +0.0 vs -0.0. No bit
//      arithmetic needed otherwise: a positive number and a negative
//      number are simply never equal.
//   2. Same sign -> |bits1 - bits2| <= 1, i.e. bit-identical or adjacent
//      representable values.
//
// Step 1 is NOT an optional safety check -- it is part of the correct
// algorithm: for ANY nonzero x, areEqual(x, -x) would otherwise
// incorrectly return true, because bits(x) - bits(-x) is exactly
// +-2^(bitwidth-1), which overflows the signed integer type and wraps
// around to a value that passes "<= 1".
//
// The 1-ULP threshold in step 2 is fixed and not configurable. It is the
// only physically-motivated choice: IEEE 754 guarantees each elementary
// operation (+, -, *, /, sqrt) is correctly rounded to within 0.5 ULP, so
// two results differing by at most 1 ULP represent the same mathematical
// value up to rounding. Any larger threshold (2, 4, ...) is an arbitrary
// epsilon in different units, with exactly the same problem this library
// is meant to avoid: it can declare numbers equal that are, in fact, the
// most they could possibly differ given that threshold.
//
// SCOPE: this compares double/float BIT PATTERNS -- i.e. values after
// decimal-to-binary rounding has already happened. It cannot recover
// distinctions between decimal inputs that already collapsed to the same
// bit pattern during that rounding (most likely in the subnormal range,
// where representable doubles are extremely sparse relative to their own
// magnitude). If your application needs to compare DECIMAL values
// (not their double/float approximations) exactly, use an arbitrary-
// precision decimal type (e.g. a BigDecimal-style library), not this
// function or any other float/double comparison -- the distinction is
// lost before any such function ever runs.
//
// PRECONDITION: neither `a` nor `b` is NaN.
// ---------------------------------------------------------------------

inline bool areEqual(double a, double b) noexcept
{
    return detail::ulp_equal<double, int64_t>(a, b);
}

inline bool areEqual(float a, float b) noexcept
{
    return detail::ulp_equal<float, int32_t>(a, b);
}

} // namespace fastcmp
