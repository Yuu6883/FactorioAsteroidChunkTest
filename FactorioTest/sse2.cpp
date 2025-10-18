#include "headers.hpp"

void update_asteroids_sse2(vector<Asteroid>& asteroids, double platform_vel) {
    update_asteroids_base_impl(asteroids, platform_vel);
}