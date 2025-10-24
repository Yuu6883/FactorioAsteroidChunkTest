#include <chrono>
#include <iostream>
#include <random>
#include <string>

#include "fpm/ios.hpp"
#include "map.hpp"

using namespace std;  // so joever
using namespace chrono;

constexpr double X_RANGE = 500.0;
constexpr double Y_RANGE = 500.0;
// 30 ticks to travel 1 tile
constexpr double V_RANGE = 1 / 30.0;
constexpr double Y_OFFSET = 0;

static inline void print_asteroid(const AsteroidStrideArray& asteroids,
                                  uint32_t index) {
    printf("pro: %d, ", asteroids.state[index] >> 16);
    printf("tbd: %s, ",
           (asteroids.flags[index].data & TBD_FLAG ? "true" : "false"));

    printf(
        "pos: (%f, %f)\n",
        asteroids.position_x[index].raw_value() / double(1 << FRACTION_BITS),
        asteroids.position_y[index].raw_value() / double(1 << FRACTION_BITS));
}

static inline uniform_int_distribution<uint16_t> proto_dist(1, 4);
static inline uniform_real_distribution<double> pos_dist_x(-X_RANGE, X_RANGE);
static inline uniform_real_distribution<double> pos_dist_y(-Y_RANGE, Y_RANGE);
static inline uniform_real_distribution<double> vel_dist(-V_RANGE, V_RANGE);

static Map* map;

static inline void populate_asteroids(vector<AsteroidDouble>& asteroids,
                                      uint32_t seed) {
    printf("Populating %zu asteroids with seed %d...", asteroids.size(), seed);
    minstd_rand rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        asteroid.prototype_id = proto_dist(rng);
        asteroid.non_game_state_index = 0;
        asteroid.flag.data = 0;
        asteroid.position.x = (pos_dist_x(rng));
        asteroid.position.y = (pos_dist_y(rng)) + Y_OFFSET;
        asteroid.velocity.x = (vel_dist(rng));
        asteroid.velocity.y = (vel_dist(rng));
    }
    printf("  done\n");
}

static inline void populate_asteroids(vector<AsteroidFixed>& asteroids,
                                      uint32_t seed) {
    printf("Populating %zu asteroids with seed %d...", asteroids.size(), seed);
    minstd_rand rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        asteroid.prototype_id = proto_dist(rng);
        asteroid.non_game_state_index = 0;
        asteroid.flag.data = 0;
        asteroid.position.x = fixed_20_11(pos_dist_x(rng));
        asteroid.position.y =
            fixed_20_11(pos_dist_y(rng)) + fixed_20_11(Y_OFFSET);
        asteroid.velocity.x = fixed_4_11(vel_dist(rng));
        asteroid.velocity.y = fixed_4_11(vel_dist(rng));
    }
    printf("  done\n");
}

static inline void populate_asteroids(AsteroidStrideArray& asteroids,
                                      uint32_t seed) {
    printf("Populating %zu asteroids with seed %d...", asteroids.size(), seed);
    minstd_rand rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        asteroids.state[i] = proto_dist(rng) << 16;
        asteroids.flags[i].data = 0;
        asteroids.position_x[i] = fixed_20_11(pos_dist_x(rng));
        asteroids.position_y[i] =
            fixed_20_11(pos_dist_y(rng)) + fixed_20_11(Y_OFFSET);
        asteroids.velocity_x[i] = fixed_4_11(vel_dist(rng));
        asteroids.velocity_y[i] = fixed_4_11(vel_dist(rng));
    }

    printf("  done\n");
}

static inline bool validate(const vector<AsteroidFixed>& a1,
                            const AsteroidStrideArray& a2) {
    if (a1.size() != a2.size()) {
        printf("Validation failed: size mismatch!\n");
        return false;
    } else
        printf("Validating %zu asteroids... ", a1.size());

    for (uint32_t i = 0; i < a1.size(); i++) {
        const auto& asteroid1 = a1[i];
        const auto& asteroid2_prot = a2.state[i] >> 16;
        const auto& asteroid2_flag = a2.flags[i].data;
        const auto& asteroid2_posx = a2.position_x[i];
        const auto& asteroid2_posy = a2.position_y[i];
        const auto& asteroid2_velx = a2.velocity_x[i];
        const auto& asteroid2_vely = a2.velocity_y[i];

        if (asteroid1.prototype_id != asteroid2_prot ||
            asteroid1.flag.data != asteroid2_flag ||
            asteroid1.position.x != asteroid2_posx ||
            asteroid1.position.y != asteroid2_posy ||
            asteroid1.velocity.x != asteroid2_velx ||
            asteroid1.velocity.y != asteroid2_vely) {
            printf("failed at index %d!\n", i);
            printf("A1[%d]: ", i);
            printf("pro: %d, ", asteroid1.prototype_id);
            printf("tbd: %s, ",
                   (asteroid1.flag.data & TBD_FLAG ? "true" : "false"));
            printf(
                "pos: (%f, %f), ",
                asteroid1.position.x.raw_value() / double(1 << FRACTION_BITS),
                asteroid1.position.y.raw_value() / double(1 << FRACTION_BITS));
            printf("\n");
            printf("A2[%d]: ", i);
            print_asteroid(a2, i);
            printf("\n");
            return false;
        }
    }

    printf("succeeded\n");
    return true;
}

static void print_map_tiles() {
    printf("Map tiles:\n");
    for (int32_t y = 0; y < map->grid_h; y++) {
        for (int32_t x = 0; x < map->grid_w; x++) {
            auto tile_index = map->tiles[x + y * map->grid_w];
            if (tile_index) {
                printf("Tile (%d, %d): \n", (x + map->x_offset),
                       (y + map->y_offset));
                for (uint32_t ty = 0; ty < 32; ty++) {
                    for (uint32_t tx = 0; tx < 32; tx++) {
                        auto tile = &map->tile_data[tile_index];
                        printf("%c", (tile->get_bit(tx, ty) ? 'X' : 'O'));
                    }
                    printf("\n");
                }
            } else {
                printf("Tile (%d, %d): empty\n", (x + map->x_offset),
                       (y + map->y_offset));
            }
        }
    }
}

#ifdef __EMSCRIPTEN__
extern "C" {
int run_bench();
}
#endif

#ifdef __EMSCRIPTEN__
int run_bench() {
#else
int main() {
#endif

    printf("Populating map...\n");
    map = new Map();

    int32_t r = 500;

    map->set(-r, r);
    map->set(r, r);
    map->set(-r, -r);
    map->set(r, -r);

    /*for (int32_t x = -1069; x <= 1069; x++) {
        for (int32_t y = -1069; y <= 5; y++) {
            map->set(x, y);
        }
    }*/

    printf("Initialized map (%f MB memory)\n",
           map->memory_usage_bytes() / 1024.f / 1024.f);

    uint32_t seed = 69420;  // unfunny
    uint32_t warmup_ticks = 8;
    uint32_t benchmark_ticks = 64 * 16 * 2;

    // double platform_vel = -1.0 / 15.0;
    double platform_vel = -1.0 / 15.0;

    uint32_t N = 1048576;
    // uint32_t N = 245000;

    vector<AsteroidDouble> a0;
    if (true) {
        a0.resize(N);
        populate_asteroids(a0, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_double(a0, map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_double(a0, map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (double "
            "precision + /Ox+SSE2).\n",
            duration, benchmark_ticks, a0.size());
    }

    vector<AsteroidFixed> a1;

    if (true) {
        a1.resize(N);
        populate_asteroids(a1, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_fixed(a1, map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_fixed(a1, map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (fixed "
            "point "
            " + /Ox+SSE2).\n",
            duration, benchmark_ticks, a1.size());
    }

    AsteroidStrideArray a2;

    if (true) {
        a2.resize(N);
        populate_asteroids(a2, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_fixed(a2, map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_fixed(a2, map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (fixed "
            "point + stride layout"
            " + /Ox+SSE2).\n",
            duration, benchmark_ticks, a2.size());
    }

#ifndef __EMSCRIPTEN__
    AsteroidStrideArray a3;

    if (true) {
        a3.resize(N);
        populate_asteroids(a3, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_avx2(a3, map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_avx2(a3, map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (stride "
            "layout + stride layout + /Ox+AVX2).\n",
            duration, benchmark_ticks, a3.size());
    }

    validate(a1, a3);
#endif
    validate(a1, a2);

    return 0;
}

static AsteroidStrideArray static_asteroids;

#ifdef __EMSCRIPTEN__
extern "C" {
#endif
EMSCRIPTEN_KEEPALIVE
void init_map() {
    if (map) return;

    printf("Populating map...\n");
    map = new Map();

    int32_t r = 500;

    map->set(-r, r);
    map->set(r, r);
    map->set(-r, -r);
    map->set(r, -r);

    printf("Initialized map (%f MB memory)\n",
           map->memory_usage_bytes() / 1024.f / 1024.f);
}
EMSCRIPTEN_KEEPALIVE
void set_asteroid_size(uint32_t size) {
    static_asteroids.resize(size);
    static_asteroids.shrink();
}
EMSCRIPTEN_KEEPALIVE
void populate_asteroids() { populate_asteroids(static_asteroids, 69420); }
EMSCRIPTEN_KEEPALIVE
void tick(double vel) { update_asteroids_fixed(static_asteroids, map, vel); }
EMSCRIPTEN_KEEPALIVE
size_t get_asteroid_size() { return static_asteroids.size(); }
EMSCRIPTEN_KEEPALIVE
void* get_asteroid_state() { return static_asteroids.state.data(); }
EMSCRIPTEN_KEEPALIVE
void* get_asteroid_pos_x() { return static_asteroids.position_x.data(); }
EMSCRIPTEN_KEEPALIVE
void* get_asteroid_pos_y() { return static_asteroids.position_y.data(); }

#ifdef __EMSCRIPTEN__
}
#endif