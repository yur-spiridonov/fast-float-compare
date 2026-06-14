// fast_compare.hpp
// A fast, branchless-friendly alternative to epsilon-based floating point
// equality comparison, based on the ordered-integer property of IEEE 754.
//
// SPDX-License-Identifier: MIT
// Author: Iouri Spiridonov

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <type_traits>

namespace fastcmp {

namespace detail {

template <typename Float, typename Int>
inline bool ulp_equal(Float a, Float b, Int max_ulp) noexcept
{
    Int bits1, bits2;
    std::memcpy(&bits1, &a, sizeof(Float));
    std::memcpy(&bits2, &b, sizeof(Float));

    Int diff = bits1 - bits2;
    if (diff < 0) diff = -diff;

    return diff <= max_ulp;
}

template <typename Float, typename Int>
inline bool ulp_equal_strict(Float a, Float b, Int max_ulp) noexcept
{
    Int bits1, bits2;
    std::memcpy(&bits1, &a, sizeof(Float));
    std::memcpy(&bits2, &b, sizeof(Float));

    // Sign bit is the top bit: negative when interpreted as signed Int.
    bool neg1 = bits1 < 0;
    bool neg2 = bits2 < 0;

    if (neg1 != neg2)
    {
        // Different signs: equal only for +0.0 == -0.0 (both magnitudes
        // are 0). Avoids the int overflow that ulp_equal can hit here.
        return (a == 0) && (b == 0);
    }

    Int diff = bits1 - bits2;
    if (diff < 0) diff = -diff;

    return diff <= max_ulp;
}

} // namespace detail

// ---------------------------------------------------------------------
// areEqual
//
// Compares two floating point numbers by reinterpreting their IEEE 754
// bit patterns as signed integers and checking how many representable
// values lie between them (ULP distance).
//
// PRECONDITIONS:
//   - Neither `a` nor `b` is NaN.
//   - This function is NOT defined to be reliable when `a` and `b` have
//     opposite signs and both lie extremely close to the smallest subnormal
//     magnitude (`std::numeric_limits<T>::denorm_min()`, i.e. around
//     ±5e-324 for double). In that case bits(a) - bits(b) overflows,
//     wraps to INT64_MIN, and areEqual incorrectly reports the two
//     numbers as equal (a false positive). This is an exceedingly narrow
//     edge case that essentially never occurs in practical computations.
//
// max_ulp = 1 is the natural, physically-motivated threshold: IEEE 754
// guarantees each elementary operation (+, -, *, /, sqrt) is correctly
// rounded to within 0.5 ULP, so two results that differ by at most 1 ULP
// represent the same mathematical value up to rounding.
// ---------------------------------------------------------------------

inline bool areEqual(double a, double b, int64_t max_ulp = 1) noexcept
{
    return detail::ulp_equal<double, int64_t>(a, b, max_ulp);
}

inline bool areEqual(float a, float b, int32_t max_ulp = 1) noexcept
{
    return detail::ulp_equal<float, int32_t>(a, b, max_ulp);
}

// ---------------------------------------------------------------------
// areEqualSafe
//
// Same as areEqual, but additionally checks for NaN inputs and returns
// false if either argument is NaN (matching IEEE 754 semantics, where
// NaN != NaN). Slightly slower due to the isnan() checks.
// ---------------------------------------------------------------------

inline bool areEqualSafe(double a, double b, int64_t max_ulp = 1) noexcept
{
    if (std::isnan(a) || std::isnan(b)) return false;
    return areEqual(a, b, max_ulp);
}

inline bool areEqualSafe(float a, float b, int32_t max_ulp = 1) noexcept
{
    if (std::isnan(a) || std::isnan(b)) return false;
    return areEqual(a, b, max_ulp);
}

// ---------------------------------------------------------------------
// areEqualStrict
//
// A modified version of areEqual that first compares the sign bits of
// `a` and `b`. If the signs differ, the numbers are equal only if both
// are zero (so +0.0 == -0.0 still holds), otherwise they are unequal.
// This avoids the integer-overflow false positive documented for
// areEqual (opposite-sign values near denorm_min()).
//
// This costs one extra branch and is the recommended choice whenever the
// inputs may be of either sign and the denorm_min() edge case matters to
// you. areEqual remains available for callers who know their inputs are
// same-signed (or non-negative) and want the absolute minimum overhead.
//
// PRECONDITION: neither `a` nor `b` is NaN (same as areEqual).
// ---------------------------------------------------------------------

inline bool areEqualStrict(double a, double b, int64_t max_ulp = 1) noexcept
{
    return detail::ulp_equal_strict<double, int64_t>(a, b, max_ulp);
}

inline bool areEqualStrict(float a, float b, int32_t max_ulp = 1) noexcept
{
    return detail::ulp_equal_strict<float, int32_t>(a, b, max_ulp);
}

inline bool areEqualStrictSafe(double a, double b, int64_t max_ulp = 1) noexcept
{
    if (std::isnan(a) || std::isnan(b)) return false;
    return areEqualStrict(a, b, max_ulp);
}

inline bool areEqualStrictSafe(float a, float b, int32_t max_ulp = 1) noexcept
{
    if (std::isnan(a) || std::isnan(b)) return false;
    return areEqualStrict(a, b, max_ulp);
}

} // namespace fastcmp
