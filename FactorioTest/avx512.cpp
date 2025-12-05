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

static inline __m512i div32(__m512i val) {
    return _mm512_srai_epi32(val, 5);  // divide by 32
}

static inline __m512i mod32(__m512i val) {
    __m512i mask = _mm512_set1_epi32(31);          // 0b11111
    __m512i result = _mm512_and_si512(val, mask);  // val & 31
    result = _mm512_add_epi32(result, _mm512_set1_epi32(32));
    result = _mm512_and_si512(result, mask);  // wrap around to [0,31]
    return result;
}

void update_asteroids_avx512(AsteroidStrideArray& asteroids,
    const Map* map, double platform_vel_double) {

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

    constexpr uint32_t ELEM = 16;

    auto platform_vel = fixed_20_11(platform_vel_double);
    uint32_t write_index = 0;
    uint32_t end = asteroids.size();

    // I hate MSVC, why can't it unroll the loop to even avx2 with a billion
    // hints??
    for (uint32_t i = 0; i < end; i += ELEM) {
        // load 8 elements at once
        __m512i px = _mm512_load_si512((__m512i*)(pos_x + i));
        __m512i py = _mm512_load_si512((__m512i*)(pos_y + i));

        __m512i vx =
            _mm512_cvtepi16_epi32(_mm256_load_si256((__m256i*)(vel_x + i)));
        __m512i vy =
            _mm512_cvtepi16_epi32(_mm256_load_si256((__m256i*)(vel_y + i)));

        // add velocities
        __m512i new_px = _mm512_add_epi32(px, vx);
        __m512i new_py = _mm512_add_epi32(
            py,
            _mm512_add_epi32(vy, _mm512_set1_epi32(platform_vel.raw_value())));

        uint16_t clamped_combined_mask;
        {
            uint16_t clamped_mask_x_low =
                _mm512_cmpgt_epi32_mask(_mm512_set1_epi32(min_x), new_px);
            uint16_t clamped_mask_x_high =
                _mm512_cmpgt_epi32_mask(new_px, _mm512_set1_epi32(max_x));
            uint16_t clamped_mask_y_low =
                _mm512_cmpgt_epi32_mask(_mm512_set1_epi32(min_y), new_py);
            uint16_t clamped_mask_y_high =
                _mm512_cmpgt_epi32_mask(new_py, _mm512_set1_epi32(max_y));

            // Combine all masks with a bitwise OR
            clamped_combined_mask = clamped_mask_x_low | clamped_mask_x_high |
                clamped_mask_y_low | clamped_mask_y_high;
        }

        __m512i min_x_vec = _mm512_set1_epi32(min_x);
        __m512i max_x_vec = _mm512_set1_epi32(max_x);
        __m512i clamped_px =
            _mm512_max_epi32(min_x_vec, _mm512_min_epi32(new_px, max_x_vec));
        clamped_px = _mm512_srai_epi32(clamped_px, FRACTION_BITS);

        __m512i min_y_vec = _mm512_set1_epi32(min_y);
        __m512i max_y_vec = _mm512_set1_epi32(max_y);
        __m512i clamped_py =
            _mm512_max_epi32(min_y_vec, _mm512_min_epi32(new_py, max_y_vec));
        clamped_py = _mm512_srai_epi32(clamped_py, FRACTION_BITS);

        __m512i cx = div32(clamped_px);
        __m512i cy = div32(clamped_py);
        __m512i tx = mod32(clamped_px);
        __m512i ty = mod32(clamped_py);

        __m512i tile_index = _mm512_add_epi32(
            _mm512_sub_epi32(cx, _mm512_set1_epi32(OX)),
            _mm512_mullo_epi32(_mm512_sub_epi32(cy, _mm512_set1_epi32(OY)),
                _mm512_set1_epi32(GW)));
        __m512i bit_index =
            _mm512_add_epi32(tx, _mm512_mullo_epi32(ty, _mm512_set1_epi32(32)));

        __m512i dx = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_set1_epi32(CENTER_X), new_px), FRACTION_BITS);
        __m512i dy = _mm512_srai_epi32(_mm512_sub_epi32(_mm512_set1_epi32(CENTER_Y), new_py), FRACTION_BITS);

        // Widen to 64-bit to prevent overflow
        __m512i new_px_lo =
            _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(dx, 0));
        __m512i new_px_hi =
            _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(dx, 1));
        __m512i new_py_lo =
            _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(dy, 0));
        __m512i new_py_hi =
            _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(dy, 1));

        __m512i vy_plus = _mm512_add_epi32(vy, _mm512_set1_epi32(platform_vel.raw_value()));

        __m512i vx_lo = _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(vx, 0));
        __m512i vx_hi = _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(vx, 1));
        __m512i vy_lo = _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(vy_plus, 0));
        __m512i vy_hi = _mm512_cvtepi32_epi64(_mm512_extracti32x8_epi32(vy_plus, 1));

        // Compute dot products
        __m512i dot_lo = _mm512_add_epi64(_mm512_mullo_epi64(new_px_lo, vx_lo),
            _mm512_mullo_epi64(new_py_lo, vy_lo));
        __m512i dot_hi = _mm512_add_epi64(_mm512_mullo_epi64(new_px_hi, vx_hi),
            _mm512_mullo_epi64(new_py_hi, vy_hi));

        // Compare dot <= 0
        __m512i zero64 = _mm512_setzero_si512();
        uint8_t cond0_lo = _mm512_cmp_epi64_mask(zero64, dot_lo, 2);  // dot <= 0 -> zero > dot
        uint8_t cond0_hi = _mm512_cmp_epi64_mask(zero64, dot_hi, 2);
        uint16_t cond0_mask = (uint16_t(cond0_hi) << 8) | cond0_lo;

        // Combine with cond1
        uint16_t bye = clamped_combined_mask & cond0_mask;

        for (uint32_t j = 0; j < ELEM; j++) {
            const Map::TileMask* tile =
                &tile_data[tile_indices[tile_index.m512i_i32[j]]];
            bool colli = tile->operator[](bit_index.m512i_i32[j]);
            bool remove = bool(colli | ((bye >> j) & 1));

            auto k = i + j;
            asteroids.state[write_index] = asteroids.state[k];
            asteroids.flags[write_index] = asteroids.flags[k];

            asteroids.position_x[write_index] = fixed_20_11::from_raw_value(
                static_cast<int32_t>(new_px.m512i_i32[j]));
            asteroids.position_y[write_index] = fixed_20_11::from_raw_value(
                static_cast<int32_t>(new_py.m512i_i32[j]));

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