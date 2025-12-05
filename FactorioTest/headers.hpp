#pragma once

#include <bitset>
#include <vector>

#include "allocator.hpp"
#include "fpm/fixed.hpp"

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

#ifdef __EMSCRIPTEN__
#ifndef VSCODE_STFU
#include <emscripten.h>
#endif
#endif

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

using std::vector;

#define FRACTION_BITS 11
using fixed_20_11 = fpm::fixed<int32_t, int64_t, FRACTION_BITS>;
using fixed_4_11 = fpm::fixed<int16_t, int32_t, FRACTION_BITS>;

template <typename T>
struct Vec {
    T x;
    T y;
};

using TilePosition = Vec<int32_t>;

constexpr uint8_t TBD_FLAG = 1 << 0;
constexpr uint8_t RSR_FLAG = 1 << 2;

struct AsteroidChunkFlag {
    uint8_t data;
    uint8_t to_be_deleted() const { return data & TBD_FLAG; };
    uint8_t is_reserved() const { return data & RSR_FLAG; }
};

struct alignas(64) AsteroidDouble {
    char pad_1[24];  // vtable in original struct
    uint16_t prototype_id;
    uint16_t non_game_state_index;
    AsteroidChunkFlag flag;
    Vec<double> position;
    Vec<double> velocity;
};

#pragma pack(push, 1)
struct alignas(32) AsteroidFixed {
    char pad_1[8];  // vtable in original struct
    uint32_t state;
    AsteroidChunkFlag flag;
    Vec<fixed_20_11> position;
    Vec<fixed_4_11> velocity;
};
#pragma pack(pop)

template <typename T>
using AlignedVector = vector<T, HugePageAllocator<T>>;

#define REMOVE_BIT_INDEX 15
#define REMOVE_BIT (1 << REMOVE_BIT_INDEX)

struct AsteroidStrideArray {
    size_t actual_size = 0;
    size_t capacity = 0;

    AlignedVector<uint32_t> state;
    AlignedVector<fixed_20_11> position_x;
    AlignedVector<fixed_20_11> position_y;
    AlignedVector<fixed_4_11> velocity_x;
    AlignedVector<fixed_4_11> velocity_y;

    inline size_t size() const { return actual_size; }

    void resize(size_t new_size) {
        int64_t old_size = actual_size;
        actual_size = new_size;
        // printf("Resizing asteroid stride array: %zu -> %zu\n", old_size,
        // new_size); round up to multiple of 4 or 8 or 16
        constexpr size_t mask = 0xF;
        capacity = new_size = (new_size + mask) & ~static_cast<size_t>(mask);

        state.resize(new_size);
        position_x.resize(new_size);
        position_y.resize(new_size);
        velocity_x.resize(new_size);
        velocity_y.resize(new_size);

        int64_t c = int64_t(new_size) - old_size;
        if (c > 0) {
            std::fill_n(&state[old_size], c, REMOVE_BIT);
            AsteroidChunkFlag zero{};
            std::fill_n(&position_x[old_size], c, fixed_20_11(0));
            std::fill_n(&position_y[old_size], c, fixed_20_11(0));
            std::fill_n(&velocity_x[old_size], c, fixed_4_11(0));
            std::fill_n(&velocity_y[old_size], c, fixed_4_11(0));
        }
    }

    void shrink() {
        state.shrink_to_fit();
        position_x.shrink_to_fit();
        position_y.shrink_to_fit();
        velocity_x.shrink_to_fit();
        velocity_y.shrink_to_fit();
    }
};

template <typename T>
static inline T select(bool cond, T a, T b) {
    T mask = -(T)cond;  // all bits 1 if cond==true, else 0
    return (T)(a & mask | b & ~mask);
}

static inline int32_t clamp(int32_t x, int32_t lo, int32_t hi) {
    // branchless: uses arithmetic shifts, no conditionals
    int32_t less = x - lo;
    int32_t more = hi - x;

    // if x < lo → (less >> 31) = -1 → returns lo
    // if x > hi → (more >> 31) = -1 → returns hi
    // else → x
    x = x - (less & (less >> 31));  // clamp lower
    x = x + (more & (more >> 31));  // clamp upper

    return x;
}

static inline uint32_t mod32(int32_t val) { return ((val & 31) + 32) & 31; };

static inline int32_t div32(int32_t val) { return val >> 5; };

class Map;

NOINLINE void update_asteroids_double(vector<AsteroidDouble>& asteroids,
                                      const Map* map, double platform_vel);

NOINLINE void update_asteroids_fixed(vector<AsteroidFixed>& asteroids,
                                     const Map* map, double platform_vel);

NOINLINE void update_asteroids_fixed(AsteroidStrideArray& asteroids,
                                     const Map* map, double platform_vel);

#ifndef __EMSCRIPTEN__
NOINLINE void update_asteroids_avx2(AsteroidStrideArray& asteroids,
                                    const Map* map, double platform_vel);
// no perf improvement, removed for failing validation somehow
// NOINLINE void update_asteroids_avx512(AsteroidStrideArray& asteroids,
//                                       const Map* map, double platform_vel);
#endif
