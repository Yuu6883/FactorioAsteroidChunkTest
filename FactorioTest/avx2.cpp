#include <memory>
#include <iostream>
#include "map.hpp"
#include "fpm/ios.hpp"

using namespace std;

#ifdef NDEBUG // NDEBUG is typically defined in release builds
#define ASSUME_ALIGNED(ptr, N) std::assume_aligned<N>(ptr)
#else
#define ASSUME_ALIGNED(ptr, N) (ptr)
#endif

void update_asteroids_avx2(AsteroidStrideArray& asteroids, const Map* map, double platform_vel_double) {
    auto pos_x = ASSUME_ALIGNED(reinterpret_cast<int32_t*>(asteroids.position_x.data()), 32);
    auto pos_y = ASSUME_ALIGNED(reinterpret_cast<int32_t*>(asteroids.position_y.data()), 32);
    const auto vel_x = ASSUME_ALIGNED(reinterpret_cast<int16_t*>(asteroids.velocity_x.data()), 32);
    const auto vel_y = ASSUME_ALIGNED(reinterpret_cast<int16_t*>(asteroids.velocity_y.data()), 32);
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

    auto tile_indices = map->tiles.data();
    auto tile_data = map->tile_data.data();

    constexpr uint32_t ELEM = 8;

    alignas(32) double new_positions_x[ELEM];
    alignas(32) double new_positions_y[ELEM];

    auto platform_vel = fixed_20_11::fixed(platform_vel_double);
    uint32_t write_index = 0;
    uint32_t end = asteroids.size();

    for (uint32_t i = 0; i < end; i += ELEM) {
        // load 8 elements at once
        __m256i px = _mm256_load_si256((__m256i*) (pos_x + i));
        __m256i py = _mm256_load_si256((__m256i*) (pos_y + i));

        __m256i vx = _mm256_cvtepi16_epi32(_mm_load_si128((__m128i*) (vel_x + i)));
        __m256i vy = _mm256_cvtepi16_epi32(_mm_load_si128((__m128i*) (vel_y + i)));

        // add velocities
        __m256i new_px = _mm256_add_epi32(px, vx);
        __m256i new_py = _mm256_add_epi32(py, _mm256_add_epi32(vy, _mm256_set1_epi32(platform_vel.raw_value())));

        __m256i min_x_vec = _mm256_set1_epi32(min_x);
        __m256i max_x_vec = _mm256_set1_epi32(max_x);
        __m256i clamped_px = _mm256_max_epi32(min_x_vec, _mm256_min_epi32(new_px, max_x_vec));
		clamped_px = _mm256_srai_epi32(clamped_px, FRACTION_BITS);

        __m256i min_y_vec = _mm256_set1_epi32(min_y);
        __m256i max_y_vec = _mm256_set1_epi32(max_y);
        __m256i clamped_py = _mm256_max_epi32(min_y_vec, _mm256_min_epi32(new_py, max_y_vec));
		clamped_py = _mm256_srai_epi32(clamped_py, FRACTION_BITS);

		__m256i cx = div32(clamped_px);
		__m256i cy = div32(clamped_py);
		__m256i tx = mod32(clamped_px);
		__m256i ty = mod32(clamped_py);

        __m256i tile_index =
            _mm256_add_epi32(
                _mm256_sub_epi32(cx, _mm256_set1_epi32(OX)),
                _mm256_mullo_epi32(
                    _mm256_sub_epi32(cy, _mm256_set1_epi32(OY)),
                    _mm256_set1_epi32(GW)
                )
            );

        __m256i bit_index =
            _mm256_add_epi32(
                tx,
                _mm256_mullo_epi32(ty, _mm256_set1_epi32(32))
			);

        for (uint32_t j = 0; j < ELEM; j++) {
			const Map::TileMask* tile = &tile_data[tile_indices[tile_index.m256i_i32[j]]];
			bool remove = tile->operator[](bit_index.m256i_i32[j]);

            auto k = i + j;
			asteroids.prototype_id[write_index] = asteroids.prototype_id[k];
			asteroids.non_game_state_index[write_index] = asteroids.non_game_state_index[k];
			asteroids.flags[write_index] = asteroids.flags[k];
			
            asteroids.position_x[write_index] = fixed_20_11::from_raw_value(static_cast<int32_t>(new_px.m256i_i32[j]));
			asteroids.position_y[write_index] = fixed_20_11::from_raw_value(static_cast<int32_t>(new_py.m256i_i32[j]));
			
            asteroids.velocity_x[write_index] = asteroids.velocity_x[k];
			asteroids.velocity_y[write_index] = asteroids.velocity_y[k];

            write_index += !remove & (k < end);
        }
    }
    
    auto removed = asteroids.size() - write_index;
    asteroids.resize(write_index);
    // cout << "Remaining asteroids: " << asteroids.size() << " (removed " << removed << " this tick) %8 = " << (write_index % 8) << endl;
}