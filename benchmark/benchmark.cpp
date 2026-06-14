// benchmark.cpp
// Compares the throughput of three floating point comparison strategies:
//   1. areEqual          (this library, ULP via integer reinterpretation)
//   2. areEqualRelative   (classic relative-epsilon, std::numeric_limits)
//   3. fabs(a-b) < eps    (naive fixed absolute epsilon)
//
// Build (release mode is important for a fair comparison):
//   g++ -O2 -std=c++17 -o benchmark benchmark.cpp

#include "../include/fast_compare.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <limits>

template <typename T>
bool areEqualRelative(T a, T b) noexcept {
    if (a == b) return true;
    const T diff = std::fabs(a - b);
    return diff <= (std::numeric_limits<T>::epsilon() * std::max(std::fabs(a), std::fabs(b)));
}

template <typename T>
bool areEqualNaiveEpsilon(T a, T b, T eps = static_cast<T>(1e-9)) noexcept {
    return std::fabs(a - b) < eps;
}

template <typename Func>
double time_it(const std::vector<double>& a, const std::vector<double>& b, Func f, int repeats)
{
    volatile bool sink = false; // prevent optimizing the loop away
    auto start = std::chrono::high_resolution_clock::now();
    for (int r = 0; r < repeats; ++r) {
        for (size_t i = 0; i < a.size(); ++i) {
            sink = f(a[i], b[i]);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    (void)sink;
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main()
{
    constexpr size_t N = 1'000'000;
    constexpr int REPEATS = 50;

    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist(-1e6, 1e6);

    std::vector<double> a(N), b(N);
    for (size_t i = 0; i < N; ++i) {
        a[i] = dist(rng);
        // b is "almost equal" to a — typical of comparing a computed
        // result against a reference value
        b[i] = a[i] + a[i] * std::numeric_limits<double>::epsilon();
    }

    double t_fast = time_it(a, b, [](double x, double y) { return fastcmp::areEqual(x, y); }, REPEATS);
    double t_rel  = time_it(a, b, [](double x, double y) { return areEqualRelative(x, y); }, REPEATS);
    double t_eps  = time_it(a, b, [](double x, double y) { return areEqualNaiveEpsilon(x, y); }, REPEATS);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "N = " << N << " comparisons x " << REPEATS << " repeats\n\n";
    std::cout << "areEqual (ULP, this lib): " << t_fast << " ms\n";
    std::cout << "areEqualRelative:         " << t_rel  << " ms\n";
    std::cout << "fabs(a-b) < 1e-9:         " << t_eps  << " ms\n\n";

    std::cout << "Speedup vs areEqualRelative: " << (t_rel / t_fast) << "x\n";
    std::cout << "Speedup vs naive epsilon:    " << (t_eps / t_fast) << "x\n";

    return 0;
}
