#pragma once

#include <bitset>
#include <vector>
#include <immintrin.h>
#include "allocator.hpp"
#include "fpm/fixed.hpp"

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
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
    uint8_t to_be_deleted() { return data & TBD_FLAG; };
    uint8_t is_reserved() { return data & RSR_FLAG; }
};

#pragma pack(push, 1)
struct alignas(32) Asteroid {
    char pad_1[8]; // vtable in original struct
    uint16_t prototype_id;
    uint16_t non_game_state_index;
    AsteroidChunkFlag flag;
    Vec<fixed_20_11> position;
    Vec<fixed_4_11> velocity;
};
#pragma pack(pop)

template <typename T>
using AlignedVector = vector<T, HugePageAllocator<T>>;

struct AsteroidStrideArray {
    size_t actual_size = 0;
    size_t capacity = 0;

    AlignedVector<uint16_t> prototype_id;
    AlignedVector<uint16_t> non_game_state_index;
    AlignedVector<AsteroidChunkFlag> flags;
    AlignedVector<fixed_20_11> position_x;
    AlignedVector<fixed_20_11> position_y;
    AlignedVector<fixed_4_11> velocity_x;
    AlignedVector<fixed_4_11> velocity_y;

    inline size_t size() const {
        return actual_size;
    }

    void resize(size_t new_size) {
        auto old_size = actual_size;
        actual_size = new_size;
		// printf("Resizing asteroid stride array: %zu -> %zu\n", old_size, new_size);
        // round up to multiple of 4 or 8
        constexpr size_t mask = 7;
        capacity = new_size = (new_size + mask) & ~static_cast<size_t>(mask);

        prototype_id.resize(new_size);
        non_game_state_index.resize(new_size);
        flags.resize(new_size);
        position_x.resize(new_size);
        position_y.resize(new_size);
        velocity_x.resize(new_size);
        velocity_y.resize(new_size);

        int64_t c = new_size - old_size;
        if (c > 0) {
            std::fill_n(&prototype_id[old_size], c, 0);
            std::fill_n(&non_game_state_index[old_size], c, 0);
            AsteroidChunkFlag zero{};
            std::fill_n(&flags[old_size], c, zero);
            std::fill_n(&position_x[old_size], c, fixed_20_11::fixed(0));
            std::fill_n(&position_y[old_size], c, fixed_20_11::fixed(0));
            std::fill_n(&velocity_x[old_size], c, fixed_4_11::fixed(0));
            std::fill_n(&velocity_y[old_size], c, fixed_4_11::fixed(0));
        }
    }
};

template<typename T>
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

static inline uint32_t mod32(int32_t val) {
    return ((val & 31) + 32) & 31;
};

static inline int32_t div32(int32_t val) {
    return val >> 5;
};

static inline __m256i div32(__m256i val) {
    return _mm256_srai_epi32(val, 5); // divide by 32
}

static inline __m256i mod32(__m256i val) {
    __m256i mask = _mm256_set1_epi32(31);        // 0b11111
    __m256i result = _mm256_and_si256(val, mask); // val & 31
    result = _mm256_add_epi32(result, _mm256_set1_epi32(32));
    result = _mm256_and_si256(result, mask);     // wrap around to [0,31]
    return result;
}

class Map;
NOINLINE void update_asteroids_sse2(vector<Asteroid>& asteroids, const Map* map, double platform_vel);
NOINLINE void update_asteroids_avx2(AsteroidStrideArray& asteroids, const Map* map, double platform_vel);
