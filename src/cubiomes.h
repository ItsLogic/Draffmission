#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct Cubiomes;
typedef struct Cubiomes Cubiomes;

typedef struct PosArea {
    int32_t x;
    int32_t z;
    int32_t area;
} PosArea;

Cubiomes *cubiomes_create(int large_biomes);
void cubiomes_free(Cubiomes *cubiomes);
void cubiomes_apply_seed(Cubiomes *cubiomes, uint64_t seed);
int cubiomes_test_monte_carlo(Cubiomes *cubiomes, int32_t x, int32_t z, int32_t range, int32_t min_area, double confidence);
int cubiomes_test_biome_centers(Cubiomes *cubiomes, int32_t x, int32_t z, int32_t range, int32_t min_area, int32_t scale, int32_t tol, PosArea *out);
int cubiomes_sample_biome(Cubiomes *cubiomes, int32_t x, int32_t y, int32_t z);
int cubiomes_get_biome_at_block(Cubiomes *cubiomes, int32_t x, int32_t y, int32_t z);
void cubiomes_voronoi_map(Cubiomes *cubiomes, int32_t x, int32_t y, int32_t z, int32_t *x4, int32_t *y4, int32_t *z4);
int cubiomes_get_biome_at(Cubiomes *cubiomes, int32_t scale, int32_t x, int32_t y, int32_t z);

#ifdef __cplusplus
}
#endif