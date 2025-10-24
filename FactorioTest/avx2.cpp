#include <immintrin.h>

#include <iostream>
#include <memory>

#include "fpm/ios.hpp"
#include "map.hpp"

using namespace std;

#ifdef NDEBUG  // NDEBUG is typically defined in release builds
#define ASSUME_ALIGNED(ptr, N) std::assume_aligned<N>(ptr)
#else
#define ASSUME_ALIGNED(ptr, N) (ptr)
#endif

static inline __m256i div32(__m256i val) {
    return _mm256_srai_epi32(val, 5);  // divide by 32
}

static inline __m256i mod32(__m256i val) {
    __m256i mask = _mm256_set1_epi32(31);          // 0b11111
    __m256i result = _mm256_and_si256(val, mask);  // val & 31
    result = _mm256_add_epi32(result, _mm256_set1_epi32(32));
    result = _mm256_and_si256(result, mask);  // wrap around to [0,31]
    return result;
}

void update_asteroids_avx2(AsteroidStrideArray& asteroids, const Map* map,
                           double platform_vel_double) {
    auto pos_x = ASSUME_ALIGNED(
        reinterpret_cast<int32_t*>(asteroids.position_x.data()), 32);
    auto pos_y = ASSUME_ALIGNED(
        reinterpret_cast<int32_t*>(asteroids.position_y.data()), 32);
    const auto vel_x = ASSUME_ALIGNED(
        reinterpret_cast<int16_t*>(asteroids.velocity_x.data()), 32);
    const auto vel_y = ASSUME_ALIGNED(
        reinterpret_cast<int16_t*>(asteroids.velocity_y.data()), 32);
    auto flags = ASSUME_ALIGNED(asteroids.flags.data(), 32);

    const Map::TileMask EMPTY_MASK{};
    // Precompute map bounds in fixed-point
    const auto min_x = (map->platform_bound.left - BORDER) << FRACTION_BITS;
    const auto max_x = (map->platform_bound.right + BORDER) << FRACTION_BITS;

    const auto min_y = (map->platform_bound.bottom - BORDER) << FRACTION_BITS;
    const auto max_y = (map->platform_bound.top + BORDER) << FRACTION_BITS;

    const auto OX = map->x_offset;
    const auto OY = map->y_offset;
    const auto GW = map->grid_w;

    const auto CENTER_X = (min_x + max_x) / 2;
    const auto CENTER_Y = (min_y + max_y) / 2;

    auto tile_indices = map->tiles.data();
    auto tile_data = map->tile_data.data();

    constexpr uint32_t ELEM = 8;

    auto platform_vel = fixed_20_11(platform_vel_double);
    uint32_t write_index = 0;
    uint32_t end = asteroids.size();

    // I hate MSVC, why can't it unroll the loop to even avx2 with a billion
    // hints??
    for (uint32_t i = 0; i < end; i += ELEM) {
        // load 8 elements at once
        __m256i px = _mm256_load_si256((__m256i*)(pos_x + i));
        __m256i py = _mm256_load_si256((__m256i*)(pos_y + i));

        __m256i vx =
            _mm256_cvtepi16_epi32(_mm_load_si128((__m128i*)(vel_x + i)));
        __m256i vy =
            _mm256_cvtepi16_epi32(_mm_load_si128((__m128i*)(vel_y + i)));

        // add velocities
        __m256i new_px = _mm256_add_epi32(px, vx);
        __m256i new_py = _mm256_add_epi32(
            py,
            _mm256_add_epi32(vy, _mm256_set1_epi32(platform_vel.raw_value())));

        __m256i clamped_combined_mask;
        {
            __m256i clamped_mask_x_low =
                _mm256_cmpgt_epi32(_mm256_set1_epi32(min_x), new_px);
            __m256i clamped_mask_x_high =
                _mm256_cmpgt_epi32(new_px, _mm256_set1_epi32(max_x));
            __m256i clamped_mask_y_low =
                _mm256_cmpgt_epi32(_mm256_set1_epi32(min_y), new_py);
            __m256i clamped_mask_y_high =
                _mm256_cmpgt_epi32(new_py, _mm256_set1_epi32(max_y));

            // Combine all masks with a bitwise OR
            clamped_combined_mask =
                _mm256_or_si256(clamped_mask_x_low, clamped_mask_x_high);
            clamped_combined_mask =
                _mm256_or_si256(clamped_combined_mask, clamped_mask_y_low);
            clamped_combined_mask =
                _mm256_or_si256(clamped_combined_mask, clamped_mask_y_high);
        }

        __m256i min_x_vec = _mm256_set1_epi32(min_x);
        __m256i max_x_vec = _mm256_set1_epi32(max_x);
        __m256i clamped_px =
            _mm256_max_epi32(min_x_vec, _mm256_min_epi32(new_px, max_x_vec));
        clamped_px = _mm256_srai_epi32(clamped_px, FRACTION_BITS);

        __m256i min_y_vec = _mm256_set1_epi32(min_y);
        __m256i max_y_vec = _mm256_set1_epi32(max_y);
        __m256i clamped_py =
            _mm256_max_epi32(min_y_vec, _mm256_min_epi32(new_py, max_y_vec));
        clamped_py = _mm256_srai_epi32(clamped_py, FRACTION_BITS);

        __m256i cx = div32(clamped_px);
        __m256i cy = div32(clamped_py);
        __m256i tx = mod32(clamped_px);
        __m256i ty = mod32(clamped_py);

        __m256i tile_index = _mm256_add_epi32(
            _mm256_sub_epi32(cx, _mm256_set1_epi32(OX)),
            _mm256_mullo_epi32(_mm256_sub_epi32(cy, _mm256_set1_epi32(OY)),
                               _mm256_set1_epi32(GW)));
        __m256i bit_index =
            _mm256_add_epi32(tx, _mm256_mullo_epi32(ty, _mm256_set1_epi32(32)));

        __m256i dx = _mm256_sub_epi32(_mm256_set1_epi32(CENTER_X) ,new_px);
        __m256i dy = _mm256_sub_epi32(_mm256_set1_epi32(CENTER_Y), new_py);

        // Widen to 64-bit to prevent overflow
        __m256i new_px_lo =
            _mm256_cvtepi32_epi64(_mm256_castsi256_si128(dx));
        __m256i new_px_hi =
            _mm256_cvtepi32_epi64(_mm256_extracti128_si256(dx, 1));
        __m256i new_py_lo =
            _mm256_cvtepi32_epi64(_mm256_castsi256_si128(dy));
        __m256i new_py_hi =
            _mm256_cvtepi32_epi64(_mm256_extracti128_si256(dy, 1));

        __m256i vx_lo = _mm256_cvtepi32_epi64(_mm256_castsi256_si128(vx));
        __m256i vx_hi = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(vx, 1));
        __m256i vy_lo = _mm256_cvtepi32_epi64(_mm256_castsi256_si128(vy));
        __m256i vy_hi = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(vy, 1));

        // Compute dot products
        __m256i dot_lo = _mm256_add_epi64(_mm256_mullo_epi64(new_px_lo, vx_lo),
                                          _mm256_mullo_epi64(new_py_lo, vy_lo));
        __m256i dot_hi = _mm256_add_epi64(_mm256_mullo_epi64(new_px_hi, vx_hi),
                                          _mm256_mullo_epi64(new_py_hi, vy_hi));

        // Compare dot <= 0
        __m256i zero64 = _mm256_setzero_si256();
        __m256i cond0_lo =
            _mm256_cmpgt_epi64(zero64, dot_lo);  // dot <= 0 -> zero > dot
        __m256i cond0_hi = _mm256_cmpgt_epi64(zero64, dot_hi);

        // Pack 64-bit mask to 32-bit (so you can combine with cond1)
        __m128i cond0_lo32 = _mm256_cvtepi64_epi32(
            cond0_lo);  // takes lower 32-bit of each 64-bit lane
        __m128i cond0_hi32 = _mm256_cvtepi64_epi32(cond0_hi);
        __m256i cond0 = _mm256_set_m128i(cond0_hi32, cond0_lo32);

        // Combine with cond1
        __m256i bye = _mm256_and_si256(clamped_combined_mask, cond0);

        for (uint32_t j = 0; j < ELEM; j++) {
            const Map::TileMask* tile =
                &tile_data[tile_indices[tile_index.m256i_i32[j]]];
            bool colli = tile->operator[](bit_index.m256i_i32[j]);
            bool remove = bool(colli | bye.m256i_i32[j]);

            auto k = i + j;
            asteroids.state[write_index] = asteroids.state[k];
            asteroids.flags[write_index] = asteroids.flags[k];

            asteroids.position_x[write_index] = fixed_20_11::from_raw_value(
                static_cast<int32_t>(new_px.m256i_i32[j]));
            asteroids.position_y[write_index] = fixed_20_11::from_raw_value(
                static_cast<int32_t>(new_py.m256i_i32[j]));

            asteroids.velocity_x[write_index] = asteroids.velocity_x[k];
            asteroids.velocity_y[write_index] = asteroids.velocity_y[k];

            write_index += !remove & (k < end);
        }
    }

    auto removed = asteroids.size() - write_index;
    asteroids.resize(write_index);
    // cout << "Remaining asteroids: " << asteroids.size() << " (removed " <<
    // removed << " this tick) %8 = " << (write_index % 8) << endl;
}