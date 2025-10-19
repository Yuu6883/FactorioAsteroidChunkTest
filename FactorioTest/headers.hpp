#pragma once

#include <vector>
#include <immintrin.h>
#include "allocator.hpp"

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

using std::vector;

template <typename T>
struct Vec {
    T x;
    T y;
};

using Vector2D = Vec<double>;
using TilePosition = Vec<int32_t>;

#pragma pack(push, 1)
struct alignas(64) Asteroid {
    char pad_1[24]; // vtable in original struct
    uint16_t prototype_id;
    uint16_t non_game_state_index;
    uint8_t to_be_deleted;
    uint8_t reserved;
    char pad_2[2]; // padding to align next member
    Vector2D position;
    Vector2D velocity;
};
#pragma pack(pop)

static_assert(sizeof(Asteroid) == 64, "Asteroid size is not 64 bytes");


template <typename T>
using AlignedVector = vector<T, HugePageAllocator<T>>;

struct AsteroidStrideArray {
    size_t actual_size;

    AlignedVector<uint16_t> prototype_id;
    AlignedVector<uint16_t> non_game_state_index;
    AlignedVector<uint8_t> to_be_deleted;
    AlignedVector<uint8_t> reserved;
    AlignedVector<double> position_x;
    AlignedVector<double> position_y;
    AlignedVector<double> velocity_x;
    AlignedVector<double> velocity_y;

    inline size_t size() const {
        return actual_size;
    }

    void resize(size_t new_size) {
        actual_size = new_size;
        // round up to multiple of 4
        new_size= (new_size + 3) & ~static_cast<size_t>(3);

        prototype_id.resize(new_size);
        non_game_state_index.resize(new_size);
        to_be_deleted.resize(new_size);
        reserved.resize(new_size);
        position_x.resize(new_size);
        position_y.resize(new_size);
        velocity_x.resize(new_size);
        velocity_y.resize(new_size);

        for (size_t i = actual_size; i < new_size; i++) {
            prototype_id[i] = 0;
            non_game_state_index[i] = 0;
            to_be_deleted[i] = 0;
            reserved[i] = 0;
            position_x[i] = 0.0;
            position_y[i] = 0.0;
            velocity_x[i] = 0.0;
            velocity_y[i] = 0.0;
        }
    }
};

extern uint32_t colli_checks;
extern uint32_t total_checks;

// Dummy collision function
NOINLINE 
static uint8_t collide_with_tiles_dummy(const TilePosition& position) {
    return position.y < -1000000;
}

template <typename T = vector<Asteroid>>
__forceinline static void update_asteroids_base_impl(T& asteroids, double platform_vel) {
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        if (asteroid.to_be_deleted != 0) {
            continue;
        }

        int32_t old_tile_x = static_cast<int32_t>(asteroid.position.x * 256.0) >> 8;
        int32_t old_tile_y = static_cast<int32_t>(asteroid.position.y * 256.0) >> 8;

        double new_px = asteroid.position.x + asteroid.velocity.x;
        double new_py = asteroid.position.y + asteroid.velocity.y + platform_vel;

        int32_t new_tile_x = static_cast<int32_t>(new_px * 256.0) >> 8;
        int32_t new_tile_y = static_cast<int32_t>(new_py * 256.0) >> 8;

        if (old_tile_x != new_tile_x || old_tile_y != new_tile_y) {
            TilePosition tile_pos{ new_tile_x, new_tile_y };
            if (collide_with_tiles_dummy(tile_pos)) {
                asteroid.to_be_deleted = 1;
                // TriggerEffect::apply(dyingTriggerEffect)
                continue;
            }
        }

        asteroid.position.x = new_px;
        asteroid.position.y = new_py;
    }
}

__forceinline static void update_asteroids_loop_unroll(AsteroidStrideArray& asteroids, double platform_vel) {

    double* pos_x = std::assume_aligned<32>(asteroids.position_x.data());
    double* pos_y = std::assume_aligned<32>(asteroids.position_y.data());
    const double* vel_x = std::assume_aligned<32>(asteroids.velocity_x.data());
    const double* vel_y = std::assume_aligned<32>(asteroids.velocity_y.data());
    uint8_t* tbd = std::assume_aligned<32>(asteroids.to_be_deleted.data());

	alignas(32) double vel = platform_vel;
    alignas(32) double new_positions_x[4];
    alignas(32) double new_positions_y[4];
    alignas(32) int32_t tile_x[4];
    alignas(32) int32_t tile_y[4];
    alignas(32) int32_t colli[4];

    for (uint32_t i = 0; i < asteroids.size(); i += 4) {
        uint32_t tbd_masks = *reinterpret_cast<uint32_t*>(tbd + i);
        if (tbd_masks == 0x01010101) continue; // unlikely but why not

        // load 4 elements at once
        __m256d px = _mm256_load_pd(pos_x + i);
        __m256d py = _mm256_load_pd(pos_y + i);
        __m256d vx = _mm256_load_pd(vel_x + i);
        __m256d vy = _mm256_load_pd(vel_y + i);

        // add velocities
        __m256d new_px = _mm256_add_pd(px, vx);
        __m256d new_py = _mm256_add_pd(py, _mm256_add_pd(vy, _mm256_set1_pd(platform_vel)));

        // scale positions by 256.0
        __m256d scale = _mm256_set1_pd(256.0);
        __m256d old_x_scaled = _mm256_mul_pd(px, scale);
        __m256d old_y_scaled = _mm256_mul_pd(py, scale);
        __m256d new_x_scaled = _mm256_mul_pd(new_px, scale);
        __m256d new_y_scaled = _mm256_mul_pd(new_py, scale);

        // convert to int32 vector
        __m128i old_xi = _mm256_cvttpd_epi32(old_x_scaled);
        __m128i old_yi = _mm256_cvttpd_epi32(old_y_scaled);
        __m128i new_xi = _mm256_cvttpd_epi32(new_x_scaled);
        __m128i new_yi = _mm256_cvttpd_epi32(new_y_scaled);

        // >> 8 to get tile positions
        old_xi = _mm_srai_epi32(old_xi, 8);
        old_yi = _mm_srai_epi32(old_yi, 8);
        new_xi = _mm_srai_epi32(new_xi, 8);
        new_yi = _mm_srai_epi32(new_yi, 8);

        __m128i coll_mask = _mm_or_si128(
            _mm_xor_si128(old_xi, new_xi),
            _mm_xor_si128(old_yi, new_yi)
        );

        // if none collides, write pos with simd store and continue, turns out to be slower
        /*if (tbd_masks == 0 && _mm_testz_si128(coll_mask, coll_mask)) {
            _mm256_storeu_pd(pos_x + i, new_px);
            _mm256_storeu_pd(pos_y + i, new_py);
            continue;
        }*/

        // break vectors to scalars
        _mm256_store_pd(new_positions_x, new_px);
        _mm256_store_pd(new_positions_y, new_py);
        _mm_store_si128((__m128i*) tile_x, new_xi);
        _mm_store_si128((__m128i*) tile_y, new_yi);
        _mm_store_si128((__m128i*) colli, coll_mask);

        // can't simd here sadly
        for (uint32_t j = 0; j < 4; j++) {
            uint32_t idx = i + j;
            // printf("[%u] tbd: %s colli: %i, tile: %i %i\n", idx, tbd[idx] ? "true" : "false", colli[j],  tile_x[j], tile_y[j]);
            if (tbd[idx]) continue;
            if (colli[j] && collide_with_tiles_dummy(TilePosition{ tile_x[j], tile_y[j] })) {
                tbd[idx] = 1;
                // TriggerEffect::apply(dyingTriggerEffect)
            } else {
                // Update positions
                pos_x[idx] = new_positions_x[j];
                pos_y[idx] = new_positions_y[j];
            }
        }
    }
}

NOINLINE void update_asteroids_debug(vector<Asteroid>& asteroids, double platform_vel);
NOINLINE void update_asteroids_sse2(vector<Asteroid>& asteroids, double platform_vel);
NOINLINE void update_asteroids_avx2(AsteroidStrideArray& asteroids, double platform_vel);
