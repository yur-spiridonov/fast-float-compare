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
//     opposite signs near the smallest subnormal magnitude (|x| close to
//     std::numeric_limits<T>::denorm_min()). In that narrow region the
//     signed-integer reinterpretation can overflow. This is an extreme
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

} // namespace fastcmp
