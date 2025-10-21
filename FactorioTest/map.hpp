#pragma once
#include "headers.hpp"

constexpr int32_t PAD_DEFAULT = 5;
constexpr int32_t BORDER = 48;

class Map {
public:
    size_t current_tick = 0;
    uint16_t grid_w = 0;
    uint16_t grid_h = 0;
    // chunk offset 
    int16_t x_offset = 0;
    int16_t y_offset = 0;

    struct AABB {
        int32_t top;
        int32_t bottom;
        int32_t left;
        int32_t right;
    } platform_bound = { 0, 0, 0, 0 };

    struct TileMask : std::bitset<32 * 32> {
        inline void set_bit(uint32_t x, uint32_t y, bool value) noexcept {
            this->operator[](x + y * 32) = value;
        }
        inline bool get_bit(uint32_t x, uint32_t y) const noexcept {
            return this->operator[](x + y * 32);
        }
    };

    vector<uint32_t> free_indices;
    AlignedVector<TileMask> tile_data;
    AlignedVector<uint32_t> tiles;

    Map() {
        tile_data.reserve(128);
        free_indices.reserve(128);
        // tile0 is specialized for empty mask
		// tile1 is specialized for full mask
        tile_data.resize(2);
        tile_data[0].reset();
        tile_data[1].set();

        set_bounds(-PAD_DEFAULT, PAD_DEFAULT, PAD_DEFAULT, -PAD_DEFAULT);

        for (int32_t i = -PAD_DEFAULT; i <= PAD_DEFAULT; i++) {
            for (int32_t j = -PAD_DEFAULT; j <= PAD_DEFAULT; j++) {
                set(i, j);
            }
		}
    }

    void set_bounds(int32_t left, int32_t right, int32_t top, int32_t bottom) {
        int32_t new_left = div32(left - BORDER);
        int32_t new_right = div32(right + BORDER);
        int32_t new_top = div32(top + BORDER);
        int32_t new_bottom = div32(bottom - BORDER);

        uint16_t new_w = uint16_t(new_right - new_left + 1);
        uint16_t new_h = uint16_t(new_top - new_bottom + 1);

        if (new_left == x_offset &&
            new_bottom == y_offset &&
            new_w == grid_w &&
            new_h == grid_h) return;

        // printf("Old bounds: L:%d R:%d T:%d B:%d\n", platform_bound.left, platform_bound.right, platform_bound.top, platform_bound.bottom);
        // printf("New bounds: L:%d R:%d T:%d B:%d\n", new_left, new_right, new_top, new_bottom);
        // printf("Resizing map tiles: %dx%d -> %dx%d\n", grid_w, grid_h, new_w, new_h);

        // Only expand the tiles array if necessary
        if (new_w * new_h > tiles.size()) tiles.resize(new_w * new_h);

        // Temporary mapping to hold old tiles positions
        vector<uint32_t> temp_tiles(new_w * new_h);

        // Copy existing tiles that overlap into temp array
        for (int y = 0; y < grid_h; y++) {
            for (int x = 0; x < grid_w; x++) {
                int old_x = x + x_offset;
                int old_y = y + y_offset;

                if (old_x >= new_left && old_x <= new_right &&
                    old_y >= new_bottom && old_y <= new_top) {
                    int nx = old_x - new_left;
                    int ny = old_y - new_bottom;
                    temp_tiles[nx + ny * new_w] = tiles[x + y * grid_w];
                } else {
                    // Free tiles that no longer fit
                    free_tile(tiles[x + y * grid_w]);
                }
            }
        }

        // Copy temp back into tiles in-place
        for (size_t i = 0; i < temp_tiles.size(); i++)
            tiles[i] = temp_tiles[i];

        // Update offsets and size
        x_offset = new_left;
        y_offset = new_bottom;
        grid_w = new_w;
        grid_h = new_h;

        // Update tile_bound without BORDER
        platform_bound.top = top;
        platform_bound.bottom = bottom;
        platform_bound.left = left;
        platform_bound.right = right;
    }

    uint32_t new_tile(bool zero = false) {
        uint32_t ret;
        if (free_indices.empty()) {
            auto old = tile_data.size();
            tile_data.reserve(old + 1);
            tile_data.resize(old + 1);
            ret = static_cast<uint32_t>(old);
        }
        else {
            auto i = free_indices[free_indices.size() - 1];
            free_indices.pop_back();
            ret = i;;
        }
        if (zero) tile_data[ret].reset();
        return ret;
    }

    void free_tile(uint32_t tile_index) {
		if (tile_index <= 1) return; // don't free tile0 or tile1
        free_indices.push_back(tile_index);
    }

    inline void set(int32_t x, int32_t y) {
        if (x < platform_bound.left || x > platform_bound.right ||
            y > platform_bound.top || y < platform_bound.bottom) {
            set_bounds(
                std::min(platform_bound.left, x),
                std::max(platform_bound.right, x),
                std::max(platform_bound.top, y),
                std::min(platform_bound.bottom, y)
            );
        }

        auto cx = div32(x);
        auto cy = div32(y);
        auto index = (cx - x_offset) + (cy - y_offset) * grid_w;
		// printf("Setting bit at (%d, %d) -> chunk (%d, %d) index %d ", x, y, cx, cy, index);

        assert(index >= 0 && index < tiles.size() - 1);
        if (!tiles[index]) {
            tiles[index] = new_tile(true);
			assert(tiles[index] > 1); // not tile0 or tile1
        }
		if (tiles[index] == 1) return; // tile is full

        auto tx = mod32(x);
        auto ty = mod32(y);
		// printf(" - local tile coords (%u, %u)\n", tx, ty);

		auto ti = tiles[index];
        tile_data[ti].set_bit(tx, ty, true);
        // collapse full tiles into tile1
        if (tile_data[ti].all()) {
            free_tile(ti);
            tiles[index] = 1;
        }
    }

    inline void unset(int32_t x, int32_t y) {
        auto cx = div32(x);
        auto cy = div32(y);
        if (cx < x_offset || cx >= x_offset + grid_w ||
            cy < y_offset || cy >= y_offset + grid_h) {
            return;
        }
        auto index = (cx - x_offset) + (cy - y_offset) * grid_w;
		assert(index >= 0 && index < tiles.size() - 1);

        if (tiles[index] == 1) {
            // expand full tile into new tile
            tiles[index] = new_tile();
            assert(tiles[index] > 1); // not tile0 or tile1
            tile_data[tiles[index]].set(); // full
		}
        if (!tiles[index]) return; // tile is empty

        auto tx = mod32(x);
        auto ty = mod32(y);
        auto ti = tiles[index];
        tile_data[ti].set_bit(tx, ty, false);

        // collapse empty tiles into tile0
        if (tile_data[ti].none()) {
            free_tile(tiles[index]);
            tiles[index] = 0;
        }
    }

    size_t memory_usage_bytes() const noexcept {
        size_t size = sizeof(Map);
        size += tile_data.size() * sizeof(TileMask);
        size += tiles.size() * sizeof(TileMask*);
        size += free_indices.size() * sizeof(uint32_t);
        return size;
	}
};