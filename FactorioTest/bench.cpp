#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <random>
#include <vector>

#include "allocator.hpp"

using namespace std;
using namespace chrono;

template <typename T>
using AlignedVector = vector<T, HugePageAllocator<T>>;

struct alignas(64) Test {
    double chance;
    uint64_t id;
    uint64_t bytes[6];
};

using TestVector = AlignedVector<Test>;
// using TestVector = vector<Test>;

constexpr uint32_t ITER = 32;
constexpr size_t TEST_ELEM = 1024 * 1024 * 8;
// do not inline constants
volatile double threshold = 0.25 / ITER;
volatile uint64_t V = 0xDEADBEEF;

void bench(TestVector& test, std::function<void(TestVector&)> func,
           const char* notes) {
    auto start = high_resolution_clock::now();

    for (size_t iter = 0; iter < ITER; iter++) func(test);

    auto end = high_resolution_clock::now();
    printf("%s: %lld milliseconds, %zu elements remain.\n", notes,
           duration_cast<milliseconds>(end - start).count(), test.size());
}

void bench1(TestVector& test) {
    size_t size = test.size();
    for (size_t i = 0; i < size;) {
        auto& t = test[i];
        t.chance -= threshold;

        // simulate read modify write
        t.bytes[4] += t.bytes[1];
        t.bytes[5] += t.bytes[2] + V;

        if (t.chance < threshold) {
            if (i + 1 < size) test[i] = std::move(test.back());
            test.pop_back();
            size--;
        } else
            i++;
    }
}

void bench2(TestVector& test) {
    size_t i = 0;
    auto newEnd = remove_if(test.begin(), test.end(), [&](Test& t) {
        t.chance -= threshold;
        // simulate read modify write
        t.bytes[4] += t.bytes[1];
        t.bytes[5] += t.bytes[2] + V;
        return t.chance < threshold;
    });
    test.erase(newEnd, test.end());
}

void bench3(TestVector& test) {
    size_t write_index = 0;
    size_t end = test.size();

    for (size_t i = 0; i < end; i++) {
        auto& t = test[i];

        double chance = t.chance - threshold;
        bool remove = chance < threshold;

        auto& dest = test[write_index];

        dest.id = t.id;
        dest.chance = chance;
        dest.bytes[0] = t.bytes[0];
        dest.bytes[1] = t.bytes[1];
        dest.bytes[2] = t.bytes[2];
        dest.bytes[3] = t.bytes[3];
        // modify in stack/registers, then write back to write_index
        dest.bytes[4] = t.bytes[4] + t.bytes[1];
        dest.bytes[5] = t.bytes[5] + t.bytes[2] + V;

        write_index += !remove;
    }
    test.resize(write_index);
}

#ifdef __EMSCRIPTEN__
extern "C" {
int run_bench();
}
#endif

#ifdef __EMSCRIPTEN__
int run_bench() {
#else
int _main() {
#endif

    TestVector t1;

    mt19937 rng(0);
    uniform_real_distribution<double> roll(0.0, 1.0);

    t1.reserve(TEST_ELEM);
    t1.resize(TEST_ELEM);

    // populate with bogus data
    for (size_t i = 0; i < t1.size(); i++) {
        t1[i].id = i;
        t1[i].chance = roll(rng);
        for (size_t j = 0; j < 7; j++) t1[i].bytes[j] = 7 * i + j;
    }
    // copy

    TestVector t2 = t1;
    TestVector t3 = t1;

    assert(reinterpret_cast<uint64_t>(t1.data()) % 64 == 0);
    assert(reinterpret_cast<uint64_t>(t2.data()) % 64 == 0);
    assert(reinterpret_cast<uint64_t>(t3.data()) % 64 == 0);

    bench(t1, &bench1, "test1 (swap back)");
    bench(t2, &bench2, "test2 (std::remove_if)");
    bench(t3, &bench3, "test3 (branchless write)");

    std::sort(t1.begin(), t1.end(),
              [](const Test& a, const Test& b) { return a.id < b.id; });
    assert(t1.size() == t2.size() && t1.size() == t3.size());

    for (size_t i = 0; i < t1.size(); i++) {
        if (t1[i].id != t2[i].id || t1[i].id != t3[i].id) {
            printf("Mismatch at index %zu: %llu vs %llu vs %llu\n", i, t1[i].id,
                   t2[i].id, t3[i].id);
            return 1;
        }
        if (t1[i].bytes[0] != t2[i].bytes[0] ||
            t1[i].bytes[0] != t3[i].bytes[0] ||
            t1[i].bytes[1] != t2[i].bytes[1] ||
            t1[i].bytes[1] != t3[i].bytes[1] ||
            t1[i].bytes[2] != t2[i].bytes[2] ||
            t1[i].bytes[2] != t3[i].bytes[2] ||
            t1[i].bytes[3] != t2[i].bytes[3] ||
            t1[i].bytes[3] != t3[i].bytes[3] ||
            t1[i].bytes[4] != t2[i].bytes[4] ||
            t1[i].bytes[4] != t3[i].bytes[4] ||
            t1[i].bytes[5] != t2[i].bytes[5] ||
            t1[i].bytes[5] != t3[i].bytes[5]) {
            printf("Byte mismatch at index %zu\n", i);
            return 1;
        }
        if (t1[i].chance != t2[i].chance || t1[i].chance != t3[i].chance) {
            printf("Chance mismatch at index %zu\n", i);
            return 1;
        }
    }
    printf("All tests passed!\n");
    return 0;
}