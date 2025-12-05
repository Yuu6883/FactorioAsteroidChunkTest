#include <chrono>
#include <iostream>
#include <random>
#include <set>
#include <string>

#include "fpm/ios.hpp"
#include "map.hpp"

using namespace std;  // so joever
using namespace chrono;

constexpr double X_RANGE = 100.0;
constexpr double Y_RANGE = 10.0;
// 30 ticks to travel 1 tile
constexpr double V_RANGE = 0.1 / 30.0;
constexpr double Y_OFFSET = 20.0;

static inline void print_asteroid(const AsteroidStrideArray& asteroids,
                                  uint32_t index) {
    printf("pro: %d, ", asteroids.state[index] >> 16);
    printf("tbd: %s, ",
           (asteroids.state[index] & REMOVE_BIT ? "true" : "false"));

    printf(
        "pos: (%f, %f)\n",
        asteroids.position_x[index].raw_value() / double(1 << FRACTION_BITS),
        asteroids.position_y[index].raw_value() / double(1 << FRACTION_BITS));
}

static inline uniform_int_distribution<uint16_t> proto_dist(0, 3);
static inline uniform_real_distribution<double> pos_dist_x(-X_RANGE, X_RANGE);
static inline uniform_real_distribution<double> pos_dist_y(-Y_RANGE, Y_RANGE);
static inline uniform_real_distribution<double> vel_dist(-V_RANGE, V_RANGE);

static Map* static_map;

static inline void populate_asteroids(vector<AsteroidDouble>& asteroids,
                                      uint32_t seed) {
    printf("Populating %zu asteroids with seed %d...", asteroids.size(), seed);
    mt19937 rng(seed);
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
    mt19937 rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        asteroid.state =
            (proto_dist(rng) << 16) | (rng() & (0xFFFF ^ REMOVE_BIT));
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
    mt19937 rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        asteroids.state[i] =
            (proto_dist(rng) << 16) | (rng() & (0xFFFF ^ REMOVE_BIT));
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
        const auto& asteroid2_stat = a2.state[i];
        const auto& asteroid2_posx = a2.position_x[i];
        const auto& asteroid2_posy = a2.position_y[i];
        const auto& asteroid2_velx = a2.velocity_x[i];
        const auto& asteroid2_vely = a2.velocity_y[i];

        if (asteroid1.state != asteroid2_stat ||
            asteroid1.position.x != asteroid2_posx ||
            asteroid1.position.y != asteroid2_posy ||
            asteroid1.velocity.x != asteroid2_velx ||
            asteroid1.velocity.y != asteroid2_vely) {
            printf("failed at index %d!\n", i);
            printf("A1[%d]: ", i);
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
    printf("static_map tiles:\n");
    for (int32_t y = 0; y < static_map->grid_h; y++) {
        for (int32_t x = 0; x < static_map->grid_w; x++) {
            auto tile_index = static_map->tiles[x + y * static_map->grid_w];
            if (tile_index) {
                printf("Tile (%d, %d): \n", (x + static_map->x_offset),
                       (y + static_map->y_offset));
                for (uint32_t ty = 0; ty < 32; ty++) {
                    for (uint32_t tx = 0; tx < 32; tx++) {
                        auto tile = &static_map->tile_data[tile_index];
                        printf("%c", (tile->get_bit(tx, ty) ? 'X' : 'O'));
                    }
                    printf("\n");
                }
            } else {
                printf("Tile (%d, %d): empty\n", (x + static_map->x_offset),
                       (y + static_map->y_offset));
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

    // printf("Populating static_map...\n");
    static_map = new Map();

    int32_t r = 1000;

    static_map->set(-r, r);
    static_map->set(r, r);
    static_map->set(-r, -r);
    static_map->set(r, -r);

    /*for (int32_t x = -1069; x <= 1069; x++) {
        for (int32_t y = -1069; y <= 5; y++) {
            static_map->set(x, y);
        }
    }*/

    // printf("Initialized static_map (%f MB memory)\n",
    //        static_map->memory_usage_bytes() / 1024.f / 1024.f);

    uint32_t seed = 69420;  // unfunny
    uint32_t warmup_ticks = 32;
    uint32_t benchmark_ticks = 64;

    // double platform_vel = -1.0 / 15.0;
    double platform_vel = -1.0 / 15.0;

    uint32_t N = 1048576 * 16;
    // uint32_t N = 245000;

    vector<AsteroidDouble> a0;
    if (false) {
        a0.resize(N);
        populate_asteroids(a0, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_double(a0, static_map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_double(a0, static_map, platform_vel);
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
            update_asteroids_fixed(a1, static_map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_fixed(a1, static_map, platform_vel);
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
            update_asteroids_fixed(a2, static_map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_fixed(a2, static_map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (fixed "
            "point + stride layout"
            " + /Ox+SSE2).\n",
            duration, benchmark_ticks, a2.size());
    }

    validate(a1, a2);

#ifndef __EMSCRIPTEN__
    AsteroidStrideArray a3;

    if (true) {
        a3.resize(N);
        populate_asteroids(a3, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_avx2(a3, static_map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_avx2(a3, static_map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (stride "
            "layout + stride layout + /Ox+AVX2).\n",
            duration, benchmark_ticks, a3.size());
    }

    validate(a1, a3);

    /*AsteroidStrideArray a4;
    if (true) {
        a4.resize(N);
        populate_asteroids(a4, seed);
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_avx512(a4, static_map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_avx512(a4, static_map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        printf(
            "Time elapsed: %lld ms for %d ticks, %zu asteroids remain (stride "
            "layout + stride layout + /Ox+AVX512).\n",
            duration, benchmark_ticks, a4.size());
    }

    validate(a1, a4);*/
#endif

    return 0;
}

static AsteroidStrideArray static_asteroids;
static mt19937 static_rng(69420u * 69420u);

static struct {
    double x_offset = 0.0;
    double x_range = X_RANGE;
    double y_offset = Y_OFFSET;
    double y_range = Y_RANGE;
    double v_range = V_RANGE;
} rng_bounds;

#ifdef __EMSCRIPTEN__
extern "C" {

static int32_t pos_x[32 * 32];
static int32_t pos_y[32 * 32];
static uint32_t state[32 * 32];

extern void notify_chunk_update(int32_t x, int32_t y, uint32_t size,
                                int32_t* pos_x, int32_t* pos_y,
                                uint32_t* state);

void check_chunk_update(int32_t chunk_x, int32_t chunk_y) {
    auto tile = static_map->get_tile(chunk_x, chunk_y);
    if (!tile || tile == &static_map->tile_data[0]) {
        notify_chunk_update(chunk_x, chunk_y, 0, nullptr, nullptr, nullptr);
        return;
    }

    uint32_t index = 0;
    for (int32_t i = 0; i < 32; i++) {
        for (int32_t j = 0; j < 32; j++) {
            if (!tile->get_bit(i, j)) continue;
            pos_x[index] =
                (fixed_20_11(chunk_x * 32 + i) + fixed_20_11(0.5)).raw_value();
            pos_y[index] =
                (fixed_20_11(chunk_y * 32 + j) + fixed_20_11(0.5)).raw_value();
            // printf("Setting tile %d, %d, x: %d, y: %d\n", i, j, pos_x[index],
            //        pos_y[index]);
            state[index] = 4 << 16;  // simulated space platform tile
            index++;
        }
    }

    notify_chunk_update(chunk_x, chunk_y, index, pos_x, pos_y, state);
}

EMSCRIPTEN_KEEPALIVE
void init_map() {
    if (static_map) return;
    static_map = new Map();

    // update all chunks
    for (int32_t y = static_map->y_offset;
         y < static_map->y_offset + int32_t(static_map->grid_h); y++) {
        for (int32_t x = static_map->x_offset;
             x < static_map->x_offset + int32_t(static_map->grid_w); x++) {
            check_chunk_update(x, y);
        }
    }
}
EMSCRIPTEN_KEEPALIVE
void set_asteroid_size(uint32_t size) {
    static_asteroids.resize(size);
    static_asteroids.shrink();
}
EMSCRIPTEN_KEEPALIVE
void tick(double vel) {
    update_asteroids_fixed(static_asteroids, static_map, vel);
}
EMSCRIPTEN_KEEPALIVE
size_t get_asteroid_size() { return static_asteroids.size(); }
EMSCRIPTEN_KEEPALIVE
void* get_asteroid_state() { return static_asteroids.state.data(); }
EMSCRIPTEN_KEEPALIVE
void* get_asteroid_pos_x() { return static_asteroids.position_x.data(); }
EMSCRIPTEN_KEEPALIVE
void* get_asteroid_pos_y() { return static_asteroids.position_y.data(); }

EMSCRIPTEN_KEEPALIVE
void brush(double x, double y, double radius, uint32_t method, bool value) {
    std::set<uint64_t> chunks;

    if (method == 0) {
        // square brush
        for (int32_t j = round(y - radius); j <= round(y + radius); j++) {
            for (int32_t i = round(x - radius); i <= round(x + radius); i++) {
                bool updated =
                    value ? static_map->set(i, j) : static_map->unset(i, j);

                if (updated) {
                    TilePosition p = {div32(i), div32(j)};
                    chunks.insert(*reinterpret_cast<uint64_t*>(&p));
                }
            }
        }
    } else if (method == 1) {
        // circle brush
        for (int32_t j = round(y - radius); j <= round(y + radius); j++) {
            for (int32_t i = round(x - radius); i <= round(x + radius); i++) {
                double dx = i - x;
                double dy = j - y;
                if (dx * dx + dy * dy > radius * radius) continue;
                bool updated =
                    value ? static_map->set(i, j) : static_map->unset(i, j);

                if (updated) {
                    TilePosition p = {div32(i), div32(j)};
                    chunks.insert(*reinterpret_cast<uint64_t*>(&p));
                }
            }
        }
    }
    if (!value) static_map->shrink_bounds();
    for (auto p : chunks) {
        TilePosition chunk = *reinterpret_cast<TilePosition*>(&p);
        check_chunk_update(chunk.x, chunk.y);
    }
}

EMSCRIPTEN_KEEPALIVE
void fill_asteroids(uint32_t upper_bound) {
    auto& rng = static_rng;
    AsteroidStrideArray& asteroids = static_asteroids;

    if (upper_bound > asteroids.size()) asteroids.resize(upper_bound);

    double l = std::max(double(static_map->platform_bound.left - BORDER),
                        rng_bounds.x_offset - rng_bounds.x_range);
    double r = std::min(double(static_map->platform_bound.right + BORDER),
                        rng_bounds.x_offset + rng_bounds.x_range);
    double b = std::max(double(static_map->platform_bound.bottom - BORDER),
                        rng_bounds.y_offset - rng_bounds.y_range);
    double t = std::min(double(static_map->platform_bound.top + BORDER),
                        rng_bounds.y_offset + rng_bounds.y_range);

    uniform_real_distribution<double> pos_dist_x(l, r);
    uniform_real_distribution<double> pos_dist_y(b, t);
    uniform_real_distribution<double> vel_dist(-rng_bounds.v_range,
                                               rng_bounds.v_range);

    for (uint32_t i = 0; i < upper_bound; i++) {
        if (!(asteroids.state[i] & REMOVE_BIT)) continue;

        asteroids.state[i] =
            (proto_dist(rng) << 16) | (rng() & (0xFFFF ^ REMOVE_BIT));
        asteroids.position_x[i] = fixed_20_11(pos_dist_x(rng));
        asteroids.position_y[i] =
            fixed_20_11(pos_dist_y(rng)) + fixed_20_11(Y_OFFSET);
        asteroids.velocity_x[i] = fixed_4_11(vel_dist(rng));
        asteroids.velocity_y[i] = fixed_4_11(vel_dist(rng));
    }
}

EMSCRIPTEN_KEEPALIVE
void update_rng(double x_offset, double y_offset, double x_range,
                double y_range, double vel) {
    rng_bounds.x_offset = x_offset;
    rng_bounds.y_offset = y_offset;
    rng_bounds.x_range = x_range;
    rng_bounds.y_range = y_range;
    rng_bounds.v_range = vel;
}

}
#endif