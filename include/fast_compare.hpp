// fast_compare.hpp
// A fast, branchless-friendly alternative to epsilon-based floating point
// equality comparison and ordering, based on the ordered-integer property
// of the IEEE 754 bit layout.
//
// SPDX-License-Identifier: MIT
// Author: Iouri Spiridonov

#pragma once

#include <cstdint>
#include <cstring>

namespace fastcmp {

namespace detail {

// ulp_equal: the core ULP-distance check, valid ONLY when bits1 and bits2
// have the same sign bit (both >= 0 or both < 0 as signed integers). Used
// internally by areEqual after the sign check. Not exposed directly,
// because calling it on opposite-sign inputs is unsafe (see areEqual's
// documentation for why).
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
        // This is the same totalOrder-consistent choice as to_ordered's
        // treatment of the two zeros (see to_ordered's documentation):
        // +0.0 and -0.0 are distinct, strictly ordered values
        // (-0.0 < +0.0), and distinct ordered values cannot be "equal".
        // Treating them as equal here -- as IEEE 754's arithmetic `==`
        // does -- would make compare3 inconsistent: lessThan(-0.0,+0.0)
        // is true, but areEqual(-0.0,+0.0) would also be true, so
        // compare3 would return 0 (equal) for a pair it also reports as
        // a < b.
        //
        // This branch is NOT optional for another reason too: without it,
        // bits1 - bits2 for x and -x of equal magnitude is exactly
        // +-2^(bitwidth-1), which overflows the signed integer and wraps
        // to the most negative representable value -- which then passes
        // "<= 1". areEqual(x, -x) would incorrectly return true for EVERY
        // nonzero x, not just values near denorm_min().
        return false;
    }

    return ulp_equal_same_sign<Int>(bits1, bits2);
}

// to_ordered: map a float's bit pattern to an unsigned integer such that
// the unsigned integer ordering matches the float ordering, across the
// full range including both signs and zero.
//
// IEEE 754 bit patterns are order-preserving (as unsigned integers) only
// for non-negative numbers. For negative numbers, increasing magnitude
// produces an increasing bit pattern, but increasing magnitude means a
// *decreasing* value. The standard fix ("flip"):
//   - non-negative (sign bit 0): OR in the sign bit (pushes the whole
//     non-negative range above the negative range).
//   - negative (sign bit 1): invert all bits (reverses the order within
//     the negative range, and maps it below the non-negative range).
// Both cases are unified as a single XOR with a mask that is either
// `sign_bit` (non-negative) or `all_ones` (negative), computed branchlessly
// via an arithmetic shift of the signed bit pattern.
//
// This maps +0.0 and -0.0 to two DIFFERENT, adjacent ordered values, with
// -0.0 ordered just below +0.0. This matches IEEE 754's totalOrder
// predicate, where -0.0 < +0.0 strictly (unlike `<`, where they compare
// equal). The two zeros differ only in the sign bit -- the same bit that
// distinguishes any other positive/negative pair -- so no special case is
// needed: a comparison that says "any positive value orders above any
// negative value" already implies +0.0 > -0.0, treating both as
// infinitesimals of their respective sign rather than as "the number
// zero" with no sign. lessThan therefore differs from `<` only on this
// one pair; see lessThan's documentation.
template <typename Float, typename Int, typename UInt>
inline UInt to_ordered(Float x) noexcept
{
    Int bits;
    std::memcpy(&bits, &x, sizeof(Float));

    UInt ubits = static_cast<UInt>(bits);
    constexpr UInt sign_bit = UInt(1) << (sizeof(UInt) * 8 - 1);
    constexpr int shift = sizeof(Int) * 8 - 1;
    UInt mask = sign_bit | static_cast<UInt>(bits >> shift);
    return ubits ^ mask;
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
//   1. Different signs -> unequal. ALWAYS, including +0.0 vs -0.0 (which
//      are distinct, strictly-ordered values under this library's
//      totalOrder-consistent semantics -- see to_ordered's documentation
//      and lessThan). No bit arithmetic needed otherwise: a positive
//      number and a negative number are simply never equal.
//   2. Same sign -> |bits1 - bits2| <= 1, i.e. bit-identical or adjacent
//      representable values.
//
// Step 1 is NOT an optional safety check -- it is part of the correct
// algorithm, for two independent reasons:
//   (a) Consistency with lessThan/compare3: lessThan(-0.0,+0.0) is true
//       (-0.0 < +0.0 under totalOrder). If areEqual(-0.0,+0.0) were also
//       true, compare3 would return 0 for a pair it also reports as
//       a < b -- a three-way comparison must not do both.
//   (b) Correctness for nonzero x: skipping this step does not just
//       mishandle an edge case -- for ANY nonzero x, areEqual(x, -x)
//       would incorrectly return true, because bits(x) - bits(-x) is
//       exactly +-2^(bitwidth-1), which overflows the signed integer type
//       and wraps around to a value that passes "<= 1".
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

// ---------------------------------------------------------------------
// lessThan
//
// A total order over IEEE 754 floats, via the same bit-reinterpretation
// idea as areEqual, using to_ordered (above) to handle the sign crossing
// correctly: any negative value orders below any positive value,
// regardless of magnitude.
//
// lessThan(a, b) == (a < b) for all non-NaN a, b, EXCEPT for the single
// pair (-0.0, +0.0): lessThan(-0.0, +0.0) is true, whereas -0.0 < +0.0 is
// false under IEEE 754's `<` (which treats them as equal). This matches
// IEEE 754's `totalOrder` predicate, where -0.0 < +0.0 strictly. The two
// zeros differ only in their sign bit -- the same bit that orders every
// other positive/negative pair -- so no special case is carved out for
// them; -0.0 and +0.0 are treated as infinitesimals of their respective
// sign, and "any positive orders above any negative" already covers them.
//
// Unlike areEqual, this is an EXACT comparison: there is no ULP tolerance.
// "Less than" has no analog of "equal up to rounding" -- either the bit
// patterns order them, or they don't.
//
// PRECONDITION: neither `a` nor `b` is NaN. NaN has no defined position in
// this ordering.
// ---------------------------------------------------------------------

inline bool lessThan(double a, double b) noexcept
{
    return detail::to_ordered<double, int64_t, uint64_t>(a)
         < detail::to_ordered<double, int64_t, uint64_t>(b);
}

inline bool lessThan(float a, float b) noexcept
{
    return detail::to_ordered<float, int32_t, uint32_t>(a)
         < detail::to_ordered<float, int32_t, uint32_t>(b);
}

// ---------------------------------------------------------------------
// compare3
//
// Three-way comparison built on lessThan and areEqual:
//   -1  if a < b
//    0  if areEqual(a, b)   (bit-identical or 1 ULP apart, same sign)
//   +1  if a > b
//
// Note this means compare3 can return 0 for two values that are not
// bit-identical (when they are 1 ULP apart, same sign). This mirrors
// areEqual's definition of "equal" -- see areEqual's documentation above.
// If you need compare3 to return 0 only for bit-identical values, compare
// to_ordered(a) and to_ordered(b) directly instead.
//
// compare3(-0.0, +0.0) == -1 (not 0): areEqual treats different-signed
// values as always unequal (including the two zeros), consistent with
// lessThan(-0.0,+0.0) == true. See areEqual's documentation, step 1.
//
// PRECONDITION: neither `a` nor `b` is NaN.
// ---------------------------------------------------------------------

inline int compare3(double a, double b) noexcept
{
    if (areEqual(a, b)) return 0;
    return lessThan(a, b) ? -1 : 1;
}

inline int compare3(float a, float b) noexcept
{
    if (areEqual(a, b)) return 0;
    return lessThan(a, b) ? -1 : 1;
}

} // namespace fastcmp
