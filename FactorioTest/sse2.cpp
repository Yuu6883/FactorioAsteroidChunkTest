#include <iostream>
#include "map.hpp"
#include "fpm/ios.hpp"

using namespace std;

void update_asteroids_sse2(vector<Asteroid>& asteroids, const Map* __restrict map, double platform_vel_double) {
    const auto platform_vel = fixed_20_11::fixed(platform_vel_double);

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

    uint32_t write_index = 0;

    for (uint32_t i = 0; i < asteroids.size(); i ++) {
        auto&& asteroid = asteroids[i];

        auto new_px = asteroid.position.x + fixed_20_11::from_raw_value(static_cast<int32_t>(asteroid.velocity.x.raw_value()));
        auto new_py = asteroid.position.y + fixed_20_11::from_raw_value(static_cast<int32_t>(asteroid.velocity.y.raw_value())) + platform_vel;

        /*volatile bool clamped = bool(
            (new_px.raw_value() < min_x) | (new_px.raw_value() > max_x) |
            (new_py.raw_value() < min_y) | (new_py.raw_value() > max_y)
        );*/

        auto clamped_px = clamp(new_px.raw_value(), min_x, max_x) >> FRACTION_BITS;
        auto clamped_py = clamp(new_py.raw_value(), min_y, max_y) >> FRACTION_BITS;
        auto cx = div32(clamped_px);
        auto cy = div32(clamped_py);
        auto tx = mod32(clamped_px);
        auto ty = mod32(clamped_py);
        auto tile_index = (cx - OX) + (cy - OY) * GW;
        // "unsafe" indexing - rely on set_bounds to function correctly to clamp x and y
        const Map::TileMask* tile = &tile_data[tile_indices[tile_index]];

        tile = tile ? tile : &EMPTY_MASK;
        bool remove = tile->get_bit(tx, ty);

        // TriggerEffect::apply(dyingTriggerEffect)

        memcpy(&asteroids[write_index], &asteroid, 16);
        asteroids[write_index].position = { new_px, new_py };
        asteroids[write_index].velocity = asteroid.velocity;

        write_index += !remove;
    }

    auto removed = asteroids.size() - write_index;
    asteroids.resize(write_index);
    // cout << "Remaining asteroids: " << asteroids.size() << " (removed " << removed << " this tick) %8 = " << (write_index % 8) << endl;
}