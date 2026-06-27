#include <stdint.h>
#include <string.h>
#include <emscripten.h>

#include "generator.h"
#include "biomes.h"
#include "util.h"

static Generator g;
static unsigned char colors[256][3];
static int *biome_cache = NULL;
static int biome_cache_size = 0;
static int initialized = 0;

EMSCRIPTEN_KEEPALIVE
void cubiomes_init(uint32_t seed_lo, uint32_t seed_hi, int large_biomes) {
    uint64_t seed = ((uint64_t)seed_hi << 32) | (uint64_t)seed_lo;
    if (!initialized) {
        initBiomeColors(colors);
        initialized = 1;
    }
    setupGenerator(&g, MC_NEWEST, large_biomes ? LARGE_BIOMES : 0);
    applySeed(&g, 0, seed);
}

EMSCRIPTEN_KEEPALIVE
void cubiomes_sample_tile(
    int origin_x, int origin_z, int y4,
    int stride_blocks, int w, int h,
    unsigned char *out_rgba)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int bx = origin_x + i * stride_blocks;
            int bz = origin_z + j * stride_blocks;
            int id = getBiomeAt(&g, 4, bx / 4, y4, bz / 4);
            if (id < 0 || id > 255) id = 0;
            int idx = (j * w + i) * 4;
            out_rgba[idx]     = colors[id][0];
            out_rgba[idx + 1] = colors[id][1];
            out_rgba[idx + 2] = colors[id][2];
            out_rgba[idx + 3] = 255;
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void cubiomes_gen_tile(
    int x4, int z4, int y4, int sx, int sz,
    unsigned char *out_rgba)
{
    int needed = sx * sz;
    if (needed > biome_cache_size) {
        if (biome_cache) free(biome_cache);
        biome_cache = (int*)malloc(needed * sizeof(int));
        biome_cache_size = needed;
    }

    Range r = {};
    r.scale = 4;
    r.x = x4;
    r.z = z4;
    r.sx = sx;
    r.sz = sz;
    r.y = y4;
    r.sy = 1;

    genBiomes(&g, biome_cache, r);

    for (int i = 0; i < needed; i++) {
        int id = biome_cache[i];
        if (id < 0 || id > 255) id = 0;
        out_rgba[i*4]     = colors[id][0];
        out_rgba[i*4 + 1] = colors[id][1];
        out_rgba[i*4 + 2] = colors[id][2];
        out_rgba[i*4 + 3] = 255;
    }
}
