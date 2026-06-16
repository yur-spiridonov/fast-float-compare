// test_compare.cpp
// Correctness tests for fast_compare.hpp

#include "../include/fast_compare.hpp"
#include <iostream>
#include <iomanip>
#include <limits>
#include <cassert>

int main()
{
    using namespace fastcmp;
    int failed = 0;

    auto check = [&](bool cond, const char* name) {
        std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
        if (!cond) ++failed;
    };

    // --- Basic equality ---
    check(areEqual(1.0, 1.0), "areEqual(1.0, 1.0) -> true");
    check(!areEqual(1.0, 2.0), "areEqual(1.0, 2.0) -> false");

    // --- Classic 0.1 + 0.2 vs 0.3 (differ by 1 ULP) ---
    double sum = 0.1 + 0.2;
    double ref = 0.3;
    check(areEqual(sum, ref), "areEqual(0.1+0.2, 0.3) -> true (1 ULP apart)");

    // --- Large magnitude ---
    double big_sum = 1.0e300 + 2.0e300;
    double big_ref = 3.0e300;
    check(areEqual(big_sum, big_ref), "areEqual(1e300+2e300, 3e300) -> true");

    // --- Small magnitude (subnormal) ---
    double small_sum = 1.0e-311 + 2.0e-311;
    double small_ref = 3.0e-311;
    check(areEqual(small_sum, small_ref), "areEqual(subnormal sum, ref) -> true");

    // --- float ---
    float fa = 0.1f, fb = 0.2f, fref = 0.3f;
    check(areEqual(fa + fb, fref), "areEqual(0.1f+0.2f, 0.3f) -> true");

    // --- Negative numbers ---
    check(areEqual(-1.0, -1.0), "areEqual(-1.0, -1.0) -> true");
    check(!areEqual(-1.0, -2.0), "areEqual(-1.0, -2.0) -> false");
    double neg_sum = -1.0 + 1e-16;
    double neg_ref = -0.9999999999999999;
    check(areEqual(neg_sum, neg_ref), "areEqual(-1.0+1e-16, -0.9999999999999999) -> true");

    // --- +0.0 and -0.0 are distinct: different signs are never equal ---
    double pos_zero = +0.0;
    double neg_zero = -0.0;
    check(!areEqual(pos_zero, neg_zero), "areEqual(+0.0, -0.0) -> false (different signs)");
    check(!areEqual(neg_zero, pos_zero), "areEqual(-0.0, +0.0) -> false");

    // --- SCOPE NOTE: decimal inputs that collapse to the same double ---
    // x1 and x2 are different decimal literals (they differ starting at
    // the 9th significant digit), but in the subnormal range the gap
    // between adjacent doubles (denorm_min ~ 4.94e-324) is large relative
    // to the values themselves, so both round to the SAME double at
    // compile time. areEqual correctly reports them as equal AS DOUBLES --
    // but the distinction between the original decimal inputs is already
    // lost before areEqual ever runs. See README "Scope" section.
    double x1 = 1.234567890987650e-311;
    double x2 = 1.23456789098787441868238613483e-311;
    bool collapsed = areEqual(x1, x2);
    std::cout << (collapsed ? "[INFO] " : "[UNEXPECTED] ")
              << "areEqual(x1, x2) = "
              << (collapsed ? "true (distinct decimal literals collapsed to the same double, as documented)"
                             : "false (literals no longer collapse!)")
              << "\n";

    std::cout << "\n" << (failed == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return failed == 0 ? 0 : 1;
}
