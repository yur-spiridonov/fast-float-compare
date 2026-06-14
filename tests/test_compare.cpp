// test_compare.cpp
// Basic correctness tests for fast_compare.hpp

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
    check(areEqual(1.0, 1.0), "1.0 == 1.0");
    check(!areEqual(1.0, 2.0), "1.0 != 2.0");

    // --- Classic 0.1 + 0.2 vs 0.3 (differ by 1 ULP) ---
    double sum = 0.1 + 0.2;
    double ref = 0.3;
    check(areEqual(sum, ref), "0.1 + 0.2 ~= 0.3 (1 ULP)");
    check(!areEqual(sum, ref, 0LL), "0.1 + 0.2 != 0.3 (0 ULP, exact)");

    // --- Large magnitude ---
    double big_sum = 1.0e300 + 2.0e300;
    double big_ref = 3.0e300;
    check(areEqual(big_sum, big_ref), "1e300 + 2e300 ~= 3e300");

    // --- Small magnitude (subnormal) ---
    double small_sum = 1.0e-311 + 2.0e-311;
    double small_ref = 3.0e-311;
    check(areEqual(small_sum, small_ref), "subnormal sum ~= ref");

    // --- float ---
    float fa = 0.1f, fb = 0.2f, fref = 0.3f;
    check(areEqual(fa + fb, fref), "float 0.1f + 0.2f ~= 0.3f");

    // --- Negative numbers ---
    check(areEqual(-1.0, -1.0), "-1.0 == -1.0");
    check(!areEqual(-1.0, -2.0), "-1.0 != -2.0");
    double neg_sum = -1.0 + 1e-16;
    double neg_ref = -0.9999999999999999;
    check(areEqual(neg_sum, neg_ref), "-1.0 + 1e-16 ~= -0.9999999999999999");

    // --- areEqualSafe with NaN ---
    double nan_val = std::numeric_limits<double>::quiet_NaN();
    check(!areEqualSafe(nan_val, nan_val), "NaN != NaN (areEqualSafe)");
    check(!areEqualSafe(nan_val, 1.0), "NaN != 1.0 (areEqualSafe)");

    // --- max_ulp customization ---
    double a = 1.0;
    double b = std::nextafter(std::nextafter(std::nextafter(a, 2.0), 2.0), 2.0); // 3 ULP away
    check(!areEqual(a, b, 1LL), "1.0 vs 1.0+3ULP, max_ulp=1 -> false");
    check(areEqual(a, b, 4LL), "1.0 vs 1.0+3ULP, max_ulp=4 -> true");

    std::cout << "\n" << (failed == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return failed == 0 ? 0 : 1;
}
