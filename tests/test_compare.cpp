// test_compare.cpp
// Correctness tests for fast_compare.hpp

#include "../include/fast_compare.hpp"
#include <iostream>
#include <iomanip>
#include <limits>
#include <vector>
#include <algorithm>
#include <random>
#include <cstring>
#include <cassert>

int main()
{
    using namespace fastcmp;
    int failed = 0;

    auto check = [&](bool cond, const char* name) {
        std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
        if (!cond) ++failed;
    };

    // =====================================================================
    // areEqual: basic equality
    // =====================================================================
    check(areEqual(1.0, 1.0), "1.0 == 1.0");
    check(!areEqual(1.0, 2.0), "1.0 != 2.0");

    // --- Classic 0.1 + 0.2 vs 0.3 (differ by 1 ULP) ---
    double sum = 0.1 + 0.2;
    double ref = 0.3;
    check(areEqual(sum, ref), "0.1 + 0.2 ~= 0.3 (1 ULP)");

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

    // --- Negative numbers, same sign ---
    check(areEqual(-1.0, -1.0), "-1.0 == -1.0");
    check(!areEqual(-1.0, -2.0), "-1.0 != -2.0");
    double neg_sum = -1.0 + 1e-16;
    double neg_ref = -0.9999999999999999;
    check(areEqual(neg_sum, neg_ref), "-1.0 + 1e-16 ~= -0.9999999999999999");

    // --- Fixed 1-ULP threshold: numbers 3 ULP apart are NOT equal ---
    double a3 = 1.0;
    double b3 = std::nextafter(std::nextafter(std::nextafter(a3, 2.0), 2.0), 2.0); // 3 ULP away
    check(!areEqual(a3, b3), "1.0 vs 1.0+3ULP -> false (fixed 1-ULP threshold)");

    // =====================================================================
    // areEqual: opposite-sign correctness (THE central correctness property)
    // =====================================================================
    // For ANY nonzero x, areEqual(x, -x) MUST be false. This is the
    // property that an implementation without the sign-bit check gets
    // wrong for every nonzero x, not just values near denorm_min().
    {
        double xs[] = {1.0, 2.0, 0.5, 100.0, 1e10, 1e-10, 1e300, 1e-300,
                       1.5, 3.14159, std::numeric_limits<double>::denorm_min(),
                       std::numeric_limits<double>::min(),
                       std::numeric_limits<double>::max()};
        bool all_ok = true;
        for (double x : xs) {
            if (areEqual(x, -x)) {
                all_ok = false;
                std::cout << "  areEqual(" << std::scientific << x << ", " << -x
                          << ") incorrectly returned true\n";
            }
        }
        check(all_ok, "areEqual(x, -x) == false for all tested nonzero x");
    }

    // --- +0.0 and -0.0 are distinct under this library's totalOrder-
    // consistent semantics (-0.0 < +0.0, so they cannot be "equal") ---
    double pos_zero = +0.0;
    double neg_zero = -0.0;
    check(!areEqual(pos_zero, neg_zero), "areEqual(+0.0, -0.0) -> false (distinct under totalOrder)");
    check(!areEqual(neg_zero, pos_zero), "areEqual(-0.0, +0.0) -> false");

    // --- denorm_min cross-sign case specifically ---
    double dmin =  std::numeric_limits<double>::denorm_min();
    double ndmin = -std::numeric_limits<double>::denorm_min();
    check(!areEqual(dmin, ndmin), "areEqual(+denorm_min, -denorm_min) -> false");

    // =====================================================================
    // areEqualSafe: NaN handling
    // =====================================================================
    double nan_val = std::numeric_limits<double>::quiet_NaN();
    check(!areEqualSafe(nan_val, nan_val), "NaN != NaN (areEqualSafe)");
    check(!areEqualSafe(nan_val, 1.0), "NaN != 1.0 (areEqualSafe)");

    // =====================================================================
    // lessThan: total order over IEEE 754 floats (totalOrder semantics)
    // =====================================================================
    check(lessThan(-2.0, -1.0), "lessThan(-2.0, -1.0) -> true");
    check(!lessThan(-1.0, -2.0), "lessThan(-1.0, -2.0) -> false");
    check(lessThan(-1.0, 1.0), "lessThan(-1.0, 1.0) -> true");
    check(!lessThan(1.0, -1.0), "lessThan(1.0, -1.0) -> false");
    check(lessThan(-1.0, 0.0), "lessThan(-1.0, 0.0) -> true");
    check(lessThan(0.0, 1.0), "lessThan(0.0, 1.0) -> true");

    // -0.0 vs +0.0: lessThan follows IEEE 754 totalOrder (-0.0 < +0.0
    // strictly), which differs from `<` (where -0.0 and +0.0 compare
    // equal). This is the one documented exception to "lessThan matches
    // <" -- see lessThan's documentation.
    check(lessThan(neg_zero, pos_zero), "lessThan(-0.0, +0.0) -> true (totalOrder, differs from <)");
    check(!lessThan(pos_zero, neg_zero), "lessThan(+0.0, -0.0) -> false");
    check((pos_zero < neg_zero) == false && (neg_zero < pos_zero) == false,
          "native: -0.0 < +0.0 and +0.0 < -0.0 are both false (sanity check on the contrast)");

    // --- Sorting a mixed-sign, nonzero vector matches std::sort with native `<` ---
    // (-0.0/+0.0 deliberately excluded: lessThan's strict ordering of the
    // two zeros means a vector containing both is not expected to compare
    // equal to a native std::sort, which leaves "equal" elements in an
    // unspecified relative order.)
    {
        std::vector<double> v  = {-2.0, 5.0, -100.0, 3.14, -3.14,
                                   100.0, 0.001, -0.001,
                                   std::numeric_limits<double>::denorm_min(),
                                   -std::numeric_limits<double>::denorm_min()};
        std::vector<double> v2 = v;

        std::sort(v.begin(),  v.end(), [](double x, double y){ return lessThan(x, y); });
        std::sort(v2.begin(), v2.end()); // native operator<

        check(v == v2, "sorting nonzero values with lessThan matches std::sort with native <");
    }

    // --- Sorting WITH both zeros: lessThan places -0.0 immediately before +0.0 ---
    {
        std::vector<double> v = {1.0, neg_zero, -1.0, pos_zero, 2.0};
        std::sort(v.begin(), v.end(), [](double x, double y){ return lessThan(x, y); });

        // Expect: -1.0, -0.0, +0.0, 1.0, 2.0
        bool order_ok = areEqual(v[0], -1.0) && areEqual(v[1], neg_zero)
                      && areEqual(v[2], pos_zero) && areEqual(v[3], 1.0) && areEqual(v[4], 2.0);
        // Confirm -0.0 specifically (not +0.0) landed at index 1, via sign bit
        bool v1_is_neg_zero = std::signbit(v[1]) && (v[1] == 0.0);
        bool v2_is_pos_zero = !std::signbit(v[2]) && (v[2] == 0.0);
        check(order_ok && v1_is_neg_zero && v2_is_pos_zero,
              "sorting with lessThan places -0.0 immediately before +0.0");
    }

    // --- Random stress test: lessThan matches native < across signs/magnitudes ---
    // The single documented exception (-0.0, +0.0) -- see lessThan's
    // documentation -- is excluded so this test is correct for any seed,
    // not just ones that happen not to hit it.
    {
        std::mt19937_64 rng(123);
        std::uniform_real_distribution<double> dist(-1e10, 1e10);
        bool all_match = true;
        for (int i = 0; i < 100000; ++i) {
            double x = dist(rng);
            double y = dist(rng);
            if (x == 0.0 && y == 0.0) continue; // skip the documented -0.0/+0.0 exception
            if (lessThan(x, y) != (x < y)) {
                all_match = false;
                break;
            }
        }
        check(all_match, "lessThan matches native < (100k random pairs, mixed signs, excluding -0.0/+0.0)");
    }

    // --- lessThanSafe with NaN ---
    check(!lessThanSafe(nan_val, 1.0), "lessThanSafe(NaN, 1.0) -> false");
    check(!lessThanSafe(1.0, nan_val), "lessThanSafe(1.0, NaN) -> false");

    // --- Random stress test across the full magnitude range, including subnormals ---
    // Same -0.0/+0.0 exclusion as above, for the same reason.
    {
        std::mt19937_64 rng(456);
        std::uniform_int_distribution<int64_t> bitdist(
            std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
        bool all_match = true;
        int nan_skipped = 0;
        for (int i = 0; i < 200000; ++i) {
            int64_t bx = bitdist(rng), by = bitdist(rng);
            double x, y;
            memcpy(&x, &bx, sizeof(double));
            memcpy(&y, &by, sizeof(double));
            if (std::isnan(x) || std::isnan(y)) { ++nan_skipped; continue; }
            if (x == 0.0 && y == 0.0) continue; // skip the documented -0.0/+0.0 exception
            if (lessThan(x, y) != (x < y)) { all_match = false; break; }
        }
        check(all_match, "lessThan matches native < (200k random bit patterns, full range, excluding -0.0/+0.0)");
    }

    // =====================================================================
    // compare3
    // =====================================================================
    check(compare3(1.0, 2.0) == -1, "compare3(1.0, 2.0) == -1");
    check(compare3(2.0, 1.0) == 1, "compare3(2.0, 1.0) == 1");
    check(compare3(1.0, 1.0) == 0, "compare3(1.0, 1.0) == 0");
    check(compare3(-1.0, 1.0) == -1, "compare3(-1.0, 1.0) == -1");
    check(compare3(1.0, -1.0) == 1, "compare3(1.0, -1.0) == 1");
    check(compare3(-2.0, -1.0) == -1, "compare3(-2.0, -1.0) == -1");
    check(compare3(sum, ref) == 0, "compare3(0.1+0.2, 0.3) == 0 (1 ULP -> areEqual)");

    // compare3(-0.0,+0.0) == -1, not 0: consistent with lessThan's
    // totalOrder (-0.0 < +0.0) and areEqual's "different signs -> never
    // equal" rule, including for the two zeros.
    check(compare3(neg_zero, pos_zero) == -1, "compare3(-0.0, +0.0) == -1");
    check(compare3(pos_zero, neg_zero) == 1, "compare3(+0.0, -0.0) == 1");

    // =====================================================================
    // SCOPE NOTE: decimal inputs that collapse to the same double
    // =====================================================================
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
