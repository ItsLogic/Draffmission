#include "cubiomes.h"

#include "../cubiomes/finders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

extern float getSpline(const Spline *sp, const float *vals);

// Inlined copies of noise functions — eliminates cross-CU function call overhead.
// These are identical to noise.c but marked static inline so the compiler can
// inline the full call chain into cubiomes_sample_biome_2y.
static inline double fast_indexedLerp(uint8_t idx, double a, double b, double c)
{
    switch (idx & 0xf) {
    case 0:  return  a + b;  case 1:  return -a + b;
    case 2:  return  a - b;  case 3:  return -a - b;
    case 4:  return  a + c;  case 5:  return -a + c;
    case 6:  return  a - c;  case 7:  return -a - c;
    case 8:  return  b + c;  case 9:  return -b + c;
    case 10: return  b - c;  case 11: return -b - c;
    case 12: return  a + b;  case 13: return -b + c;
    case 14: return -a + b;  default: return -b - c;
    }
}

static inline double fast_samplePerlin(const PerlinNoise *noise, double d1, double d2, double d3,
        double yamp, double ymin)
{
    uint8_t h1, h2, h3;
    double t1, t2, t3;

    if (d2 == 0.0) {
        d2 = noise->d2; h2 = noise->h2; t2 = noise->t2;
    } else {
        d2 += noise->b;
        double i2 = floor(d2);
        d2 -= i2;
        h2 = (int) i2;
        t2 = d2*d2*d2 * (d2 * (d2*6.0-15.0) + 10.0);
    }

    d1 += noise->a;
    d3 += noise->c;

    double i1 = floor(d1);
    double i3 = floor(d3);
    d1 -= i1;
    d3 -= i3;

    h1 = (int) i1;
    h3 = (int) i3;

    t1 = d1*d1*d1 * (d1 * (d1*6.0-15.0) + 10.0);
    t3 = d3*d3*d3 * (d3 * (d3*6.0-15.0) + 10.0);

    if (yamp) {
        double yclamp = ymin < d2 ? ymin : d2;
        d2 -= floor(yclamp / yamp) * yamp;
    }

    const uint8_t *idx = noise->d;

    typedef struct vec2 { uint8_t a, b; } vec2;
    vec2 v1 = { idx[h1], idx[h1+1] };
    v1.a += h2; v1.b += h2;
    vec2 v2 = { idx[v1.a], idx[v1.a+1] };
    vec2 v3 = { idx[v1.b], idx[v1.b+1] };
    v2.a += h3; v2.b += h3; v3.a += h3; v3.b += h3;
    vec2 v4 = { idx[v2.a], idx[v2.a+1] };
    vec2 v5 = { idx[v2.b], idx[v2.b+1] };
    vec2 v6 = { idx[v3.a], idx[v3.a+1] };
    vec2 v7 = { idx[v3.b], idx[v3.b+1] };

    double l1 = fast_indexedLerp(v4.a, d1,   d2,   d3);
    double l5 = fast_indexedLerp(v4.b, d1,   d2,   d3-1);
    double l2 = fast_indexedLerp(v6.a, d1-1, d2,   d3);
    double l6 = fast_indexedLerp(v6.b, d1-1, d2,   d3-1);
    double l3 = fast_indexedLerp(v5.a, d1,   d2-1, d3);
    double l7 = fast_indexedLerp(v5.b, d1,   d2-1, d3-1);
    double l4 = fast_indexedLerp(v7.a, d1-1, d2-1, d3);
    double l8 = fast_indexedLerp(v7.b, d1-1, d2-1, d3-1);

    l1 = lerp(t1, l1, l2);
    l3 = lerp(t1, l3, l4);
    l5 = lerp(t1, l5, l6);
    l7 = lerp(t1, l7, l8);
    l1 = lerp(t2, l1, l3);
    l5 = lerp(t2, l5, l7);
    return lerp(t3, l1, l5);
}

static inline double fast_sampleOctave(const OctaveNoise *noise, double x, double y, double z)
{
    double v = 0;
    for (int i = 0; i < noise->octcnt; i++) {
        const PerlinNoise *p = noise->octaves + i;
        double lf = p->lacunarity;
        double ax = maintainPrecision(x * lf);
        double ay = maintainPrecision(y * lf);
        double az = maintainPrecision(z * lf);
        v += p->amplitude * fast_samplePerlin(p, ax, ay, az, 0, 0);
    }
    return v;
}

static inline double fast_sampleDoublePerlin(const DoublePerlinNoise *noise, double x, double y, double z)
{
    const double f = 337.0 / 331.0;
    double v = fast_sampleOctave(&noise->octA, x, y, z);
    v += fast_sampleOctave(&noise->octB, x*f, y*f, z*f);
    return v * noise->amplitude;
}

struct Cubiomes {
    Generator g;
};

Cubiomes *cubiomes_create(int large_biomes) {
    Cubiomes *cubiomes = malloc(sizeof(Cubiomes));
    if (cubiomes == NULL) {
        fprintf(stderr, "cubiomes_create failed\n");
        abort();
    }
    setupGenerator(&cubiomes->g, MC_NEWEST, large_biomes ? LARGE_BIOMES : 0);
    return cubiomes;
}

void cubiomes_free(Cubiomes *cubiomes) {
    free(cubiomes);
}

void cubiomes_apply_seed(Cubiomes *cubiomes, uint64_t seed) {
    applySeed(&cubiomes->g, DIM_OVERWORLD, seed);
}

static int eval(Generator *g, int scale, int x, int y, int z, void *data) {
    return sampleBiomeNoise(&g->bn, NULL, x, y, z, NULL, 0) == mushroom_fields;
}

static Range make_range(int32_t x, int32_t z, int32_t range, int32_t scale) {
    return (Range){
        .scale = scale,
        .x = (x - range / 2) / scale,
        .z = (z - range / 2) / scale,
        .sx = range / scale,
        .sz = range / scale,
        .y = 256 / scale,
        .sy = 1
    };
}

struct locate_info_t
{
    Generator *g;
    int *ids;
    Range r;
    int match, tol;
    volatile char *stop;
};

static
int floodFillGen(struct locate_info_t *info, int i, int j, Pos *p)
{
    typedef struct { int i, j, d; } entry_t;
    entry_t *queue = (entry_t*) malloc(info->r.sx*info->r.sz * sizeof(*queue));
    int qn = 1;
    queue->i = i;
    queue->j = j;
    queue->d = 0;
    int64_t sumx = 0;
    int64_t sumz = 0;
    int n = 0;
    while (--qn >= 0)
    {
        if (info->stop && *info->stop)
        {
            free(queue);
            return 0;
        }
        int d = queue[qn].d;
        i = queue[qn].i;
        j = queue[qn].j;
        int k = j * info->r.sx + i;
        int id = info->ids[k];
        if (id == INT_MAX)
            continue;
        info->ids[k] = INT_MAX;
        int x = info->r.x + i;
        int z = info->r.z + j;
        if (info->g->mc >= MC_1_18)
            id = getBiomeAt(info->g, info->r.scale, x, info->r.y, z);
        if (id == info->match)
        {
            sumx += x;
            sumz += z;
            n++;
            d = 0;
        }
        else
        {
            if (++d >= info->tol)
                continue;
        }
        entry_t next[] = { {i,j-1,d}, {i,j+1,d}, {i-1,j,d}, {i+1,j,d} };
        for (k = 0; k < 4; k++)
        {
            i = next[k].i; j = next[k].j;
            if (i < 0 || i >= info->r.sx || j < 0 || j >= info->r.sz)
                continue;
            if (info->ids[j * info->r.sx + i] == INT_MAX)
                continue;
            queue[qn++] = next[k];
        }
    }
    free(queue);
    if (n)
    {
        p->x = (int) round((sumx / (double)n + 0.5) * info->r.scale);
        p->z = (int) round((sumz / (double)n + 0.5) * info->r.scale);
    }
    return n;
}

static
int getBiomeCentersOpt(Pos *pos, int *siz, int nmax, Generator *g, Range r,
    int match, int minsiz, int tol, int step, volatile char *stop)
{
    if (minsiz <= 0)
        minsiz = 1;
    int i, j, k, n = 0;
    int *ids = (int*) malloc(r.sx*r.sz * sizeof(int));
    memset(ids, -1, r.sx*r.sz * sizeof(int));
    if (tol <= 0)
        tol = 1;
    if (step <= 0)
        step = 1;
    struct locate_info_t info;
    info.g = g;
    info.ids = ids;
    info.r = r;
    info.stop = stop;
    info.match = match;
    info.tol = tol;

    if (g->mc >= MC_1_18)
    {
        const int *lim = getBiomeParaLimits(g->mc, match);

        int para[] = {
            NP_TEMPERATURE,
            NP_HUMIDITY,
            NP_EROSION,
            NP_CONTINENTALNESS,
            NP_WEIRDNESS,
        };
        int npara = sizeof(para) / sizeof(para[0]);
        if (step == 1)
            step = 1 + floor(sqrt(minsiz) * 0.5);

        for (j = 0; j < r.sz; j += step)
        {
            for (i = 0; i < r.sx; i += step)
            {
                if (stop && *stop)
                    break;
                for (k = 0; k < npara; k++)
                {
                    const int *plim = lim + 2*para[k];
                    if (plim[0] == INT_MIN && plim[1] == INT_MAX)
                        continue;
                    DoublePerlinNoise *dpn = &g->bn.climate[para[k]];
                    double px = (r.x+i) * r.scale / 4.0;
                    double pz = (r.z+j) * r.scale / 4.0;
                    int p = 10000 * sampleDoublePerlin(dpn, px, 0, pz);
                    if (p < plim[0] || p > plim[1])
                    {
                        ids[j*r.sx + i] = -2;
                        break;
                    }
                }
            }
        }
        match = -1; // id entries that are still -1 are our candidates
    }
    else // 1.17-
    {
        int ts = 32 / r.scale;
        if (r.sx + r.sz < 32)
            ts = 8;

        int tx = (int) floor(r.x / (double)ts);
        int tz = (int) floor(r.z / (double)ts);
        int tw = (int) ceil((r.x+r.sx) / (double)ts) - tx;
        int th = (int) ceil((r.z+r.sz) / (double)ts) - tz;
        int ti, tj;

        BiomeFilter bf;
        setupBiomeFilter(&bf, g->mc, 0, &match, 1, 0, 0, 0, 0);
        //applySeed(g, 0, g->seed);

        Range tr = { r.scale, 0, 0, ts, ts, 0, 1 };
        int *cache = allocCache(g, r);

        for (tj = 0; tj < th; tj++)
        {
            for (ti = 0; ti < tw; ti++)
            {
                if (stop && *stop)
                    break;
                tr.x = (tx+ti) * ts;
                tr.z = (tz+tj) * ts;
                if (checkForBiomes(g, cache, tr, DIM_OVERWORLD, g->seed,
                    &bf, stop) != 1)
                {
                    continue;
                }
                for (j = 0; j < ts; j++)
                {
                    int jj = tr.z + j - r.z;
                    if (jj < 0 || jj >= r.sz)
                        continue;
                    for (i = 0; i < ts; i++)
                    {
                        int ii = tr.x + i - r.x;
                        if (ii < 0 || ii >= r.sx)
                            continue;
                        ids[jj*r.sx + ii] = cache[j*tr.sx + i];
                    }
                }
            }
        }
        free(cache);
    }

    // applySeed(g, DIM_OVERWORLD, g->seed);
    for (j = 0; j < r.sz; j += step)
    {
        for (i = 0; i < r.sx; i += step)
        {
            if (stop && *stop)
                break;
            if (ids[j*r.sx + i] != match)
                continue;
            Pos center;
            int area = floodFillGen(&info, i, j, &center);
            if (area >= minsiz)
            {
                pos[n] = center;
                if (siz) siz[n] = area;
                if (++n >= nmax)
                    goto L_end;
            }
        }
    }

L_end:
    free(ids);

    return n;
}

int cubiomes_test_monte_carlo(Cubiomes *cubiomes, int32_t x, int32_t z, int32_t range, int32_t min_area, double confidence) {
    Range r = make_range(x, z, range, 4);
    double fraction = (double)min_area / (r.sx * r.sz * r.scale * r.scale);
    uint64_t rng = cubiomes->g.seed;
    return monteCarloBiomes(&cubiomes->g, r, &rng, fraction, confidence, eval, NULL);
}

int cubiomes_test_biome_centers(Cubiomes *cubiomes, int32_t x, int32_t z, int32_t range, int32_t min_area, int32_t scale, int32_t tol, PosArea *out) {
    Pos pos;
    int siz;
    Range r = make_range(x, z, range, scale);
    int minsiz = min_area / (scale * scale);
    int n = getBiomeCentersOpt(&pos, &siz, 1, &cubiomes->g, r, mushroom_fields, minsiz, tol, 0, NULL);
    if (n == 1) {
        if (out) {
            *out = (PosArea){
                .x = pos.x,
                .z = pos.z,
                .area = siz * (scale * scale),
            };
        }
        return 1;
    }
    return 0;
}

int cubiomes_get_mushroom_cont_max(Cubiomes *cubiomes) {
    const int *lim = getBiomeParaLimits(cubiomes->g.mc, mushroom_fields);
    return lim[2 * NP_CONTINENTALNESS + 1];
}

void cubiomes_sample_biome_2y(Cubiomes *cubiomes, int32_t x4, int32_t z4, int cont_max, int *biome_15, int *biome_16) {
    BiomeNoise *bn = &cubiomes->g.bn;

    double px = x4, pz = z4;
    px += fast_sampleDoublePerlin(&bn->climate[NP_SHIFT], x4, 0, z4) * 4.0;
    pz += fast_sampleDoublePerlin(&bn->climate[NP_SHIFT], z4, x4, 0) * 4.0;

    float c = fast_sampleDoublePerlin(&bn->climate[NP_CONTINENTALNESS], px, 0, pz);

    if ((int)(10000.0F * c) > cont_max) {
        *biome_15 = 0;
        *biome_16 = 0;
        return;
    }

    // Full computation for potential mushroom cells (~5% of grid)
    float e = fast_sampleDoublePerlin(&bn->climate[NP_EROSION], px, 0, pz);
    float w = fast_sampleDoublePerlin(&bn->climate[NP_WEIRDNESS], px, 0, pz);

    float np_param[] = { c, e, -3.0F * (fabsf(fabsf(w) - 0.6666667F) - 0.33333334F), w };
    double off = getSpline(bn->sp, np_param) + 0.015F;

    float t = fast_sampleDoublePerlin(&bn->climate[NP_TEMPERATURE], px, 0, pz);
    float h = fast_sampleDoublePerlin(&bn->climate[NP_HUMIDITY], px, 0, pz);

    {
        float d = 1.0 - (15 * 4) / 128.0 - 83.0/160.0 + off;
        int64_t np[6] = {
            (int64_t)(10000.0F*t), (int64_t)(10000.0F*h), (int64_t)(10000.0F*c),
            (int64_t)(10000.0F*e), (int64_t)(10000.0F*d), (int64_t)(10000.0F*w)
        };
        *biome_15 = climateToBiome(bn->mc, (const uint64_t*)np, NULL);
    }
    {
        float d = 1.0 - (16 * 4) / 128.0 - 83.0/160.0 + off;
        int64_t np[6] = {
            (int64_t)(10000.0F*t), (int64_t)(10000.0F*h), (int64_t)(10000.0F*c),
            (int64_t)(10000.0F*e), (int64_t)(10000.0F*d), (int64_t)(10000.0F*w)
        };
        *biome_16 = climateToBiome(bn->mc, (const uint64_t*)np, NULL);
    }
}

int cubiomes_sample_biome(Cubiomes *cubiomes, int32_t x, int32_t y, int32_t z) {
    return sampleBiomeNoise(&cubiomes->g.bn, NULL, x, y, z, NULL, 0);
}

int cubiomes_get_biome_at_block(Cubiomes *cubiomes, int32_t x, int32_t y, int32_t z) {
    Generator *g = &cubiomes->g;
    int x4, y4, z4;
    voronoiAccess3D(g->sha, x, y, z, &x4, &y4, &z4);
    return sampleBiomeNoise(&g->bn, NULL, x4, y4, z4, NULL, 0);
}

void cubiomes_voronoi_map(Cubiomes *cubiomes, int32_t x, int32_t y, int32_t z, int32_t *x4, int32_t *y4, int32_t *z4) {
    voronoiAccess3D(cubiomes->g.sha, x, y, z, (int*)x4, (int*)y4, (int*)z4);
}

int cubiomes_get_biome_at(Cubiomes *cubiomes, int32_t scale, int32_t x, int32_t y, int32_t z) {
    return getBiomeAt(&cubiomes->g, scale, x, y, z);
}