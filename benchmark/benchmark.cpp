// benchmark.cpp
// Compares CPU-cycle cost of:
//   1. areEqual          (this library: sign check + ULP via integer reinterpretation)
//   2. areEqualRelative   (classic relative-epsilon, std::numeric_limits)
//   3. fabs(a-b) < eps    (naive fixed absolute epsilon)
//   4. lessThan           (this library: total order via to_ordered)
//   5. operator<          (native double comparison)
//   6. compare3           (this library: three-way comparison)
//
// Uses __rdtsc with lfence serialization and median-of-medians, which is
// far more stable than wall-clock timing in a shared/virtualized
// environment: wall-clock measurements of these functions can vary by
// 2-3x between runs depending on competing processes, while the cycle
// counts below are stable to within a few percent run-to-run.
//
// x86/x64 only (uses <x86intrin.h> and __rdtsc).
//
// Build (release mode + native arch are both important):
//   g++ -O2 -std=c++17 -march=native -o benchmark benchmark.cpp

#include "../include/fast_compare.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <x86intrin.h>

template <typename T>
bool areEqualRelative(T a, T b) noexcept {
    if (a == b) return true;
    const T diff = std::fabs(a - b);
    return diff <= (std::numeric_limits<T>::epsilon() * std::max(std::fabs(a), std::fabs(b)));
}

template <typename T>
int compare3Native(T a, T b) noexcept {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

// Median cycles/call: time a tight loop over N elements (so rdtsc overhead
// is amortized over N calls), repeat for `trials` independent loops, and
// take the median of those per-trial averages. The median rejects outlier
// trials hit by interrupts/context switches without needing the loop
// itself to be free of such interruptions.
template <typename Func>
double median_cycles_per_call(Func f, const std::vector<double>& a, const std::vector<double>& b, int trials)
{
    const size_t n = a.size();
    std::vector<double> samples(trials);
    volatile int sink = 0;

    for (int t = 0; t < trials; ++t) {
        _mm_lfence();
        uint64_t start = __rdtsc();
        _mm_lfence();

        for (size_t i = 0; i < n; ++i) {
            sink = static_cast<int>(f(a[i], b[i]));
        }

        _mm_lfence();
        uint64_t end = __rdtsc();
        _mm_lfence();

        samples[t] = static_cast<double>(end - start) / static_cast<double>(n);
    }
    (void)sink;

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

int main()
{
    constexpr size_t N = 100'000;  // large enough to amortize rdtsc/lfence overhead,
                                    // small enough to stay cache-resident
    constexpr int TRIALS = 50;

    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist(-1e6, 1e6);

    // --- Equality workload: b is "almost equal" to a (1 ULP apart) ---
    std::vector<double> a(N), b(N);
    for (size_t i = 0; i < N; ++i) {
        a[i] = dist(rng);
        b[i] = a[i] + a[i] * std::numeric_limits<double>::epsilon();
    }

    // --- Ordering workload: c, d are independent random doubles, mixed signs ---
    std::vector<double> c(N), d(N);
    for (size_t i = 0; i < N; ++i) {
        c[i] = dist(rng);
        d[i] = dist(rng);
    }

    std::cout << std::fixed << std::setprecision(3);

    std::cout << "=== Equality (median cycles/call, " << N << " elements x " << TRIALS << " trials) ===\n\n";
    double t_areEqual = median_cycles_per_call([](double x, double y){ return fastcmp::areEqual(x, y); }, a, b, TRIALS);
    double t_rel      = median_cycles_per_call([](double x, double y){ return areEqualRelative(x, y); }, a, b, TRIALS);
    double t_eps      = median_cycles_per_call([](double x, double y){ return std::fabs(x - y) < 1e-9; }, a, b, TRIALS);

    std::cout << "areEqual (this lib):     " << t_areEqual << " cycles\n";
    std::cout << "areEqualRelative:        " << t_rel      << " cycles\n";
    std::cout << "fabs(a-b) < 1e-9:        " << t_eps       << " cycles\n";

    std::cout << "\n=== Ordering (median cycles/call, " << N << " elements x " << TRIALS << " trials) ===\n\n";
    double t_lt    = median_cycles_per_call([](double x, double y){ return fastcmp::lessThan(x, y); }, c, d, TRIALS);
    double t_lt_n  = median_cycles_per_call([](double x, double y){ return x < y; }, c, d, TRIALS);
    double t_cmp3  = median_cycles_per_call([](double x, double y){ return fastcmp::compare3(x, y); }, c, d, TRIALS);
    double t_cmp3n = median_cycles_per_call([](double x, double y){ return compare3Native(x, y); }, c, d, TRIALS);

    std::cout << "lessThan (this lib):     " << t_lt    << " cycles\n";
    std::cout << "operator< (native):      " << t_lt_n  << " cycles\n";
    std::cout << "compare3 (this lib):     " << t_cmp3  << " cycles\n";
    std::cout << "compare3 (native </>):   " << t_cmp3n << " cycles\n";

    return 0;
}
