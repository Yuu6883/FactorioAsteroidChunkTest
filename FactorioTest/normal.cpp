#include <iostream>

#include "fpm/ios.hpp"
#include "map.hpp"

using namespace std;

constexpr double TO_DOUBLE = 1.0 / (1 << FRACTION_BITS);

void update_asteroids_double(vector<AsteroidDouble>& asteroids,
                             const Map* __restrict map, double platform_vel) {
    const Map::TileMask EMPTY_MASK{};
    // Precompute map bounds in fixed-point
    const auto min_x = (map->platform_bound.left - BORDER);
    const auto max_x = (map->platform_bound.right + BORDER);

    const auto min_y = (map->platform_bound.bottom - BORDER);
    const auto max_y = (map->platform_bound.top + BORDER);

    const auto OX = map->x_offset;
    const auto OY = map->y_offset;
    const auto GW = map->grid_w;

    const auto CENTER_X = (min_x + max_x) / 2;
    const auto CENTER_Y = (min_y + max_y) / 2;

    auto tile_indices = map->tiles.data();
    auto tile_data = map->tile_data.data();

    uint32_t write_index = 0;
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        auto new_px = asteroid.position.x + asteroid.velocity.x;
        auto new_py = asteroid.position.y + asteroid.velocity.y + platform_vel;

        bool clamped = bool((new_px < min_x) | (new_px > max_x) |
                            (new_py < min_y) | (new_py > max_y));

        auto clamped_px = clamp(new_px, min_x, max_x);
        auto clamped_py = clamp(new_py, min_y, max_y);
        auto cx = div32(clamped_px);
        auto cy = div32(clamped_py);
        auto tx = mod32(clamped_px);
        auto ty = mod32(clamped_py);
        auto tile_index = (cx - OX) + (cy - OY) * GW;
        // "unsafe" indexing - rely on set_bounds to function correctly to clamp
        // x and y
        const Map::TileMask* tile = &tile_data[tile_indices[tile_index]];

        auto dx = CENTER_X - new_px;
        auto dy = CENTER_Y - new_py;
        int64_t dot = dx * asteroid.velocity.x + dy * asteroid.velocity.y;
        auto bye = clamped & (dot <= 0);

        tile = tile ? tile : &EMPTY_MASK;
        bool colli = tile->get_bit(tx, ty);
        bool remove = bool(colli | bye);

        // TriggerEffect::apply(dyingTriggerEffect)

        memcpy(&asteroids[write_index], &asteroid, 32);
        asteroids[write_index].position = {new_px, new_py};
        asteroids[write_index].velocity = asteroid.velocity;

        write_index += !remove;
    }

    auto removed = asteroids.size() - write_index;
    asteroids.resize(write_index);
    // cout << "Remaining asteroids: " << asteroids.size() << " (removed " <<
    // removed << " this tick) %8 = " << (write_index % 8) << endl;
}

void update_asteroids_fixed(vector<AsteroidFixed>& asteroids,
                            const Map* __restrict map,
                            double platform_vel_double) {
    const auto platform_vel = fixed_20_11(platform_vel_double).raw_value();

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

    uint32_t write_index = 0;

    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        // raw values
        auto vx = static_cast<int32_t>(asteroid.velocity.x.raw_value());
        auto vy = static_cast<int32_t>(asteroid.velocity.y.raw_value());

        auto new_px = asteroid.position.x.raw_value() + vx;
        auto new_py = asteroid.position.y.raw_value() + vy + platform_vel;

        bool clamped = bool((new_px < min_x) | (new_px > max_x) |
                            (new_py < min_y) | (new_py > max_y));

        auto clamped_px = clamp(new_px, min_x, max_x) >> FRACTION_BITS;
        auto clamped_py = clamp(new_py, min_y, max_y) >> FRACTION_BITS;
        auto cx = div32(clamped_px);
        auto cy = div32(clamped_py);
        auto tx = mod32(clamped_px);
        auto ty = mod32(clamped_py);
        auto tile_index = (cx - OX) + (cy - OY) * GW;
        // "unsafe" indexing - rely on set_bounds to function correctly to clamp
        // x and y
        const Map::TileMask* tile = &tile_data[tile_indices[tile_index]];

        int64_t dx = (CENTER_X - new_px) >> FRACTION_BITS;
        int64_t dy = (CENTER_Y - new_py) >> FRACTION_BITS;
        int64_t dot = int64_t(dx) * int64_t(vx) +
                      int64_t(dy) * int64_t(vy + platform_vel);
        auto bye = clamped & (dot <= 0);

        tile = tile ? tile : &EMPTY_MASK;
        bool colli = tile->get_bit(tx, ty);
        bool remove = bool(colli | bye);

        // TriggerEffect::apply(dyingTriggerEffect)

        memcpy(&asteroids[write_index], &asteroid, 16);
        asteroids[write_index].position.x = fixed_20_11::from_raw_value(new_px);
        asteroids[write_index].position.y = fixed_20_11::from_raw_value(new_py);
        asteroids[write_index].velocity = asteroid.velocity;

        write_index += !remove;
    }

    auto removed = asteroids.size() - write_index;
    asteroids.resize(write_index);
    // cout << "Remaining asteroids: " << asteroids.size() << " (removed " <<
    // removed << " this tick) %8 = " << (write_index % 8) << endl;
}

static uint32_t tick = 0;

void update_asteroids_fixed(AsteroidStrideArray& asteroids,
                            const Map* __restrict map,
                            double platform_vel_double) {
    const auto platform_vel = fixed_20_11(platform_vel_double).raw_value();

    const Map::TileMask EMPTY_MASK{};
    // Precompute map bounds in fixed-point
    const auto min_x = (map->platform_bound.left - BORDER) << FRACTION_BITS;
    const auto max_x = (map->platform_bound.right + BORDER) << FRACTION_BITS;

    const auto min_y = (map->platform_bound.bottom - BORDER) << FRACTION_BITS;
    const auto max_y = (map->platform_bound.top + BORDER) << FRACTION_BITS;

    const auto OX = map->x_offset;
    const auto OY = map->y_offset;
    const auto GW = map->grid_w;

    const int64_t CENTER_X = (min_x + max_x) / 2;
    const int64_t CENTER_Y = (min_y + max_y) / 2;

    auto tile_indices = map->tiles.data();
    auto tile_data = map->tile_data.data();

    uint32_t write_index = 0;
    uint32_t end = asteroids.size();

    for (uint32_t i = 0; i < end; i++) {
        // if (asteroids.state[i] & REMOVE_BIT) continue;

        // raw values
        auto vx = static_cast<int32_t>(asteroids.velocity_x[i].raw_value());
        auto vy = static_cast<int32_t>(asteroids.velocity_y[i].raw_value());

        auto new_px = asteroids.position_x[i].raw_value() + vx;
        auto new_py = asteroids.position_y[i].raw_value() + vy + platform_vel;

        bool clamped = bool((new_px < min_x) | (new_px > max_x) |
                            (new_py < min_y) | (new_py > max_y));

        auto clamped_px = clamp(new_px, min_x, max_x) >> FRACTION_BITS;
        auto clamped_py = clamp(new_py, min_y, max_y) >> FRACTION_BITS;
        auto cx = div32(clamped_px);
        auto cy = div32(clamped_py);
        auto tx = mod32(clamped_px);
        auto ty = mod32(clamped_py);
        auto tile_index = (cx - OX) + (cy - OY) * GW;
        // "unsafe" indexing - rely on set_bounds to function correctly to clamp
        // x and y
        const Map::TileMask* tile = &tile_data[tile_indices[tile_index]];

        int64_t dx = (CENTER_X - new_px) >> FRACTION_BITS;
        int64_t dy = (CENTER_Y - new_py) >> FRACTION_BITS;
        int64_t dot = int64_t(dx) * int64_t(vx) +
                      int64_t(dy) * int64_t(vy + platform_vel);
        auto bye = clamped & (dot <= 0);

        tile = tile ? tile : &EMPTY_MASK;
        bool colli = tile->get_bit(tx, ty);
        bool remove = bool(colli | bye);

        // TriggerEffect::apply(dyingTriggerEffect)

        asteroids.state[i] |= uint16_t(remove) << REMOVE_BIT_INDEX;
        ;
        asteroids.position_x[i] = fixed_20_11::from_raw_value(new_px);
        asteroids.position_y[i] = fixed_20_11::from_raw_value(new_py);

        /*
        asteroids.state[write_index] = asteroids.state[i];
        asteroids.flags[write_index].data = asteroids.flags[i].data;
        asteroids.position_x[write_index] = fixed_20_11::from_raw_value(new_px);
        asteroids.position_y[write_index] = fixed_20_11::from_raw_value(new_py);
        asteroids.velocity_x[write_index] = asteroids.velocity_x[i];
        asteroids.velocity_y[write_index] = asteroids.velocity_y[i];

        write_index += !remove;
        */
    }

    // asteroids.resize(write_index);

    tick++;

    if (tick % 32) return;

    {
        uint32_t write_index = 0;
        for (uint32_t i = 0; i < end; i++) {
            const auto flags = asteroids.state[write_index] =
                asteroids.state[i];
            asteroids.position_x[write_index] = asteroids.position_x[i];
            asteroids.position_y[write_index] = asteroids.position_y[i];
            asteroids.velocity_x[write_index] = asteroids.velocity_x[i];
            asteroids.velocity_y[write_index] = asteroids.velocity_y[i];
            write_index += 1 - ((flags >> REMOVE_BIT_INDEX) & 1);
        }
        asteroids.resize(write_index);
    }
}