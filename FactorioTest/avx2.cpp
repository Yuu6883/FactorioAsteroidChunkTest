#include "headers.hpp"

void update_asteroids_avx2(AsteroidStrideArray& asteroids, double platform_vel) {
    update_asteroids_loop_unroll(asteroids, platform_vel);
}