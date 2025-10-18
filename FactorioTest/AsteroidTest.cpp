#include <random>
#include <iostream>
#include <chrono>
#include <string>
#include "headers.hpp"

using namespace std; // so joever
using namespace chrono;

uniform_int_distribution<uint16_t> proto_dist(1, 4);
bernoulli_distribution bool_dist(0.05);
uniform_real_distribution<double> pos_dist(-1000.0, 1000.0);
uniform_real_distribution<double> vel_dist(-1.0, 1.0);

static inline void populate_asteroids(vector<Asteroid>& asteroids, uint32_t seed) {
    mt19937 rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        auto&& asteroid = asteroids[i];

        asteroid.prototype_id = proto_dist(rng);
        asteroid.non_game_state_index = 0;
        asteroid.to_be_deleted = bool_dist(rng) ? 1 : 0;
        asteroid.reserved = 0;
        asteroid.position.x = pos_dist(rng);
        asteroid.position.y = pos_dist(rng);
        asteroid.velocity.x = vel_dist(rng);
        asteroid.velocity.y = vel_dist(rng);
    }
}

static inline void populate_asteroids(AsteroidStrideArray& asteroids, uint32_t seed) {
    mt19937 rng(seed);
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        asteroids.prototype_id[i] = proto_dist(rng);
        asteroids.non_game_state_index[i] = 0;
        asteroids.to_be_deleted[i] = bool_dist(rng) ? 1 : 0;
        asteroids.reserved[i] = 0;
        asteroids.position_x[i] = pos_dist(rng);
        asteroids.position_y[i] = pos_dist(rng);
        asteroids.velocity_x[i] = vel_dist(rng);
        asteroids.velocity_y[i] = vel_dist(rng);
    }
}

static inline void print_asteroid(const Asteroid& asteroid) {
    cout << "pro: " << asteroid.prototype_id << ", ";
    cout << "tbd: " << (asteroid.to_be_deleted ? "true" : "false") << ", ";
    cout << "pos: (" << asteroid.position.x << ", " << asteroid.position.y << "), ";
    cout << "vel: (" << asteroid.velocity.x << ", " << asteroid.velocity.y << ")";
}

static inline void print_asteroid(const AsteroidStrideArray& asteroids, uint32_t index) {
    cout << "pro: " << asteroids.prototype_id[index] << ", ";
    cout << "tbd: " << (asteroids.to_be_deleted[index] ? "true" : "false") << ", ";
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
        update_asteroids_debug(asteroids, -11.5);
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
        update_asteroids_avx2(asteroids, -11.5);
    }
    cout << "After update:" << endl;
    for (uint32_t i = 0; i < asteroids.size(); i++) {
        cout << "Asteroid [" << i << "]: ";
        print_asteroid(asteroids, i);
        cout << endl;
    }

    asteroids.resize(old_size);
}

constexpr double EPSILON = 0.000001;

static inline void validate(const vector<Asteroid>& a1, const AsteroidStrideArray& a2) {
    if (a1.size() != a2.size()) {
        cout << "validation failed: size mismatch!" << endl;
        return;
    } else cout << "validating " << a1.size() << " asteroids... ";

    for (uint32_t i = 0; i < a1.size(); i++) {
        const auto& asteroid1 = a1[i];
        const auto& asteroid2_pro = a2.prototype_id[i];
        const auto& asteroid2_tbd = a2.to_be_deleted[i];
        const auto& asteroid2_posx = a2.position_x[i];
        const auto& asteroid2_posy = a2.position_y[i];
        const auto& asteroid2_velx = a2.velocity_x[i];
        const auto& asteroid2_vely = a2.velocity_y[i];

        if (asteroid1.prototype_id != asteroid2_pro ||
            asteroid1.to_be_deleted != asteroid2_tbd ||
            abs(asteroid1.position.x - asteroid2_posx) > EPSILON ||
            abs(asteroid1.position.y - asteroid2_posy) > EPSILON ||
            abs(asteroid1.velocity.x - asteroid2_velx) > EPSILON ||
            abs(asteroid1.velocity.y - asteroid2_vely) > EPSILON) {

            cout << "failed at index " << i << "!" << endl;
            cout << "A1[" << i << "]: ";
            print_asteroid(asteroid1);
            cout << endl;
            cout << "A2[" << i << "]: ";
            print_asteroid(a2, i);
            cout << endl;
            return;
        }
    }

    cout << "succeeded" << endl;
}

int main() {
    uint32_t seed = 69420; // unfunny
    uint32_t warmup_ticks = 10;
    uint32_t benchmark_ticks = 1000;

    double platform_vel = -4.0 / 60.0;

    uint32_t N = 1048576;

    vector<Asteroid> a1;
    a1.resize(N);

    AsteroidStrideArray a2;
    a2.resize(N);

    // test(a1);
    // test(a2);

    if (false) {
        populate_asteroids(a1, seed); 
        update_asteroids_debug(a1, platform_vel); \
        for (uint32_t i = 0; i < warmup_ticks; i++) 
            update_asteroids_debug(a1, platform_vel);
        auto start = high_resolution_clock::now(); 
        for (uint32_t i = 0; i < benchmark_ticks; i++) 
            update_asteroids_debug(a1, platform_vel);
        auto end = high_resolution_clock::now(); 
        auto duration = duration_cast<milliseconds>(end - start).count(); 
        cout << "time elapsed: " << duration << " ms for " << benchmark_ticks << " ticks (normal layout)." << endl; 
    } 

    if (true) {
        populate_asteroids(a1, seed);
        update_asteroids_sse2(a1, platform_vel); \
            for (uint32_t i = 0; i < warmup_ticks; i++)
                update_asteroids_sse2(a1, platform_vel);
        auto start = high_resolution_clock::now();
        for (uint32_t i = 0; i < benchmark_ticks; i++)
            update_asteroids_sse2(a1, platform_vel);
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        cout << "time elapsed: " << duration << " ms for " << benchmark_ticks << " ticks (normal layout + /Ox+SSE2)." << endl;
    }

    if (true) {
        populate_asteroids(a2, seed); 
        update_asteroids_avx2(a2, platform_vel);
        for (uint32_t i = 0; i < warmup_ticks; i++) 
            update_asteroids_avx2(a2, platform_vel);
        auto start = high_resolution_clock::now(); 
        for (uint32_t i = 0; i < benchmark_ticks; i++) 
            update_asteroids_avx2(a2, platform_vel);
        auto end = high_resolution_clock::now(); 
        auto duration = duration_cast<milliseconds>(end - start).count(); 
        cout << "time elapsed: " << duration << " ms for " << benchmark_ticks << " ticks (stride layout/loop unroll + /Ox+AVX2)." << endl; 
    }

    validate(a1, a2);

    /*printf("Fast path hit rate: %d / %d (%.2f%%)\n", fast_path_hit, fast_path_total,
        fast_path_total > 0 ? (static_cast<double>(fast_path_hit) / static_cast<double>(fast_path_total)) * 100.0 : 0.0);*/

    return 0;
}