#include <random>
#include <iostream>
#include <chrono>
#include <string>

#include "map.hpp"
#include "fpm/ios.hpp"

using namespace std; // so joever
using namespace chrono;

constexpr double X_RANGE = 1000.0;
constexpr double Y_RANGE = 50.0;
// 30 ticks to travel 1 tile
constexpr double V_RANGE = 25 / 30.0;
constexpr fixed_20_11 Y_OFFSET = fixed_20_11::fixed(0);

uniform_int_distribution<uint16_t> proto_dist(1, 4);
uniform_real_distribution<double> pos_dist_x(-X_RANGE, X_RANGE);
uniform_real_distribution<double> pos_dist_y(PAD_DEFAULT, Y_RANGE);
uniform_real_distribution<double> vel_dist(-V_RANGE, V_RANGE);

static Map* map;

static inline void populate_asteroids(vector<Asteroid>& asteroids, uint32_t seed) {
    cout << "Populating " << asteroids.size() << " asteroids with seed " << seed << "...";
    mt19937 rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        asteroid.prototype_id = proto_dist(rng);
        asteroid.non_game_state_index = 0;
        asteroid.flag.data = 0;
        asteroid.position.x = fixed_20_11::fixed(pos_dist_x(rng));
        asteroid.position.y = fixed_20_11::fixed(pos_dist_y(rng)) + Y_OFFSET;
        asteroid.velocity.x = fixed_4_11(vel_dist(rng));
        asteroid.velocity.y = fixed_4_11(vel_dist(rng));
    }
	cout << "  done" << endl;
}

static inline void populate_asteroids(AsteroidStrideArray& asteroids, uint32_t seed) {
    cout << "Populating " << asteroids.size() << " asteroids with seed " << seed << "...";
    mt19937 rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        asteroids.prototype_id[i] = proto_dist(rng);
        asteroids.non_game_state_index[i] = 0;
        asteroids.flags[i].data = 0;
        asteroids.position_x[i] = fixed_20_11::fixed(pos_dist_x(rng));
        asteroids.position_y[i] = fixed_20_11::fixed(pos_dist_y(rng)) + Y_OFFSET;
        asteroids.velocity_x[i] = fixed_4_11(vel_dist(rng));
        asteroids.velocity_y[i] = fixed_4_11(vel_dist(rng));
    }
    cout << "  done" << endl;
}

static inline void print_asteroid(const Asteroid& asteroid) {
    cout << "pro: " << asteroid.prototype_id << ", ";
    cout << "tbd: " << (asteroid.flag.data & TBD_FLAG ? "true" : "false") << ", ";
    cout << "pos: (" << asteroid.position.x << ", " << asteroid.position.y << "), ";
    cout << "vel: (" << asteroid.velocity.x << ", " << asteroid.velocity.y << ")";
}

static inline void print_asteroid(const AsteroidStrideArray& asteroids, uint32_t index) {
    cout << "pro: " << asteroids.prototype_id[index] << ", ";
    cout << "tbd: " << (asteroids.flags[index].data & TBD_FLAG ? "true" : "false") << ", ";
    cout << "pos: (" << asteroids.position_x[index] << ", " << asteroids.position_y[index] << "), ";
    cout << "vel: (" << asteroids.velocity_x[index] << ", " << asteroids.velocity_y[index] << ")";
}

static inline void test(vector<Asteroid>& asteroids) {
    cout << "Testing normal layout asteroid array..." << endl;
    auto old_size = asteroids.size();

    asteroids.resize(5);
    populate_asteroids(asteroids, 0);
    cout << "Initial asteroids:" << endl;
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        cout << "Asteroid [" << i << "]: ";
        print_asteroid(asteroids[i]);
        cout << endl;
    }
    for (uint32_t i = 0; i < 1; i++) {
        update_asteroids_sse2(asteroids, map, -11.5);
    }
    cout << "After update:" << endl;
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        cout << "Asteroid [" << i << "]: ";
        print_asteroid(asteroids[i]);
        cout << endl;
    }

    asteroids.resize(old_size);
}

static inline void test(AsteroidStrideArray& asteroids) {
    cout << "Testing stride layout asteroid array..." << endl;
    auto old_size = asteroids.size();

    asteroids.resize(5);
    populate_asteroids(asteroids, 0);
    cout << "Initial asteroids:" << endl;
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        cout << "Asteroid [" << i << "]: ";
        print_asteroid(asteroids, i);
        cout << endl;
    }
    for (uint32_t i = 0; i < 1; i++) {
        update_asteroids_avx2(asteroids, map, -11.5);
    }
    cout << "After update:" << endl;
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        cout << "Asteroid [" << i << "]: ";
        print_asteroid(asteroids, i);
        cout << endl;
    }

    asteroids.resize(old_size);
}

static inline bool validate(const vector<Asteroid>& a1, const AsteroidStrideArray& a2) {
    if (a1.size() != a2.size()) {
        cout << "Validation failed: size mismatch!" << endl;
        return false;
    } else cout << "Validating " << a1.size() << " asteroids... ";

    for (uint32_t i = 0; i < a1.size(); i++) {
        const auto& asteroid1 = a1[i];
        const auto& asteroid2_prot = a2.prototype_id[i];
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

            cout << "failed at index " << i << "!" << endl;
            cout << "A1[" << i << "]: ";
            print_asteroid(asteroid1);
            cout << endl;
            cout << "A2[" << i << "]: ";
            print_asteroid(a2, i);
            cout << endl;
            return false;
        }
    }

    cout << "succeeded" << endl;
    return true;
}

static void print_map_tiles() {
    cout << "Map tiles:" << endl;
    for (int32_t y = 0; y < map->grid_h; y++) {
        for (int32_t x = 0; x < map->grid_w; x++) {
            auto tile_index = map->tiles[x + y * map->grid_w];
            if (tile_index) {
                cout << "Tile (" << (x + map->x_offset) << ", " << (y + map->y_offset) << "): " << endl;
                for (uint32_t ty = 0; ty < 32; ty++) {
                    for (uint32_t tx = 0; tx < 32; tx++) {
						auto tile = &map->tile_data[tile_index];
                        cout << (tile->get_bit(tx, ty) ? 'X' : 'O');
                    }
                    cout << endl;
                }
            } else {
                cout << "Tile (" << (x + map->x_offset) << ", " << (y + map->y_offset) << "): empty" << endl;
            }
        }
	}
}

int main() {
	cout << "Populating map..." << endl;
	map = new Map();

    for (int32_t x = -1069; x <= 1069; x++) {
        for (int32_t y = -1069; y <= 5; y++) {
            map->set(x, y);
        }
    }

	cout << "Initialized map (" << map->memory_usage_bytes() / 1024.f / 1024.f << " MB memory)" << endl;

    uint32_t seed = 69420; // unfunny
    uint32_t warmup_ticks = 10;
    uint32_t benchmark_ticks = 1000;

    double platform_vel = -1.0 / 15.0;

    uint32_t N = 1048576;

    vector<Asteroid> a1;
    AsteroidStrideArray a2;

    if (true) {
        a1.resize(N);
        populate_asteroids(a1, seed);
            for (uint32_t i = 0; i < warmup_ticks; i++)
                update_asteroids_sse2(a1, map, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_sse2(a1, map, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        cout << "Time elapsed: " << duration << " ms for " << benchmark_ticks << " ticks (normal layout + /Ox+SSE2)." << endl;
    }

    if (true) {
        a2.resize(N);
        populate_asteroids(a2, seed); 
        for (uint32_t i = 0; i < warmup_ticks; i++)
            update_asteroids_avx2(a2, map, platform_vel);
        auto start = high_resolution_clock::now(); 
        for (uint32_t i = 0; i < benchmark_ticks; i++) 
            update_asteroids_avx2(a2, map, platform_vel);
        auto end = high_resolution_clock::now(); 
        auto duration = duration_cast<milliseconds>(end - start).count(); 
        cout << "Time elapsed: " << duration << " ms for " << benchmark_ticks << " ticks (stride layout + /Ox+AVX2)." << endl; 
    }

    validate(a1, a2);

    return 0;
}