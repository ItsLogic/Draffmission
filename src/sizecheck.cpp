#include "cpu.h"
#include "cubiomes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>

int main(int argc, char **argv) {
    int64_t worldseed = 0;
    int32_t x = 0, z = 0;
    int32_t min_size = 0;
    int has_mode = 0, large = 0;

    // Positional: sizecheck --sb|--lb <seed> <x> <z>
    // Or named: sizecheck --sb --worldseed <seed> --x <x> --z <z>
    int positionals[3] = {};
    int n_pos = 0;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--sb") == 0) {
            large = 0; has_mode = 1;
        } else if (std::strcmp(argv[i], "--lb") == 0) {
            large = 1; has_mode = 1;
        } else if (i + 1 < argc && (std::strcmp(argv[i], "--worldseed") == 0 || std::strcmp(argv[i], "--seed") == 0)) {
            worldseed = std::strtoll(argv[++i], NULL, 10);
        } else if (i + 1 < argc && std::strcmp(argv[i], "--x") == 0) {
            x = std::strtol(argv[++i], NULL, 10);
        } else if (i + 1 < argc && std::strcmp(argv[i], "--z") == 0) {
            z = std::strtol(argv[++i], NULL, 10);
        } else if (i + 1 < argc && std::strcmp(argv[i], "--size") == 0) {
            min_size = std::atoi(argv[++i]);
        } else {
            // Positional argument
            if (n_pos < 3) {
                positionals[n_pos++] = i;
            }
        }
    }

    if (!has_mode) {
        std::fprintf(stderr, "Usage: %s --sb|--lb <seed> <x> <z> [--size <min_size>]\n", argv[0]);
        return 1;
    }

    // Fill from positionals if not set via named args
    if (n_pos >= 1 && !worldseed && !x && !z) {
        worldseed = std::strtoll(argv[positionals[0]], NULL, 10);
    }
    if (n_pos >= 2 && !x) {
        x = std::strtol(argv[positionals[1]], NULL, 10);
    }
    if (n_pos >= 3 && !z) {
        z = std::strtol(argv[positionals[2]], NULL, 10);
    }

    if (min_size == 0) {
        min_size = large ? 10000000 : 1000000;
    }

    Cubiomes *cubiomes = cubiomes_create(large);
    cubiomes_apply_seed(cubiomes, (uint64_t)worldseed);

    auto t0 = std::chrono::steady_clock::now();

    int32_t range = 12800 * (large ? 4 : 1);

    PosArea res;
    if (!cubiomes_test_biome_centers(cubiomes, x, z, range, min_size, 4, 2, &res)) {
        std::printf("No mushroom island (>= %d blocks) found near (%d, %d) on seed %lld.\n", min_size, x, z, (long long)worldseed);
        cubiomes_free(cubiomes);
        return 1;
    }

    int32_t radius = (int32_t)(3.0 * std::sqrt((double)res.area / M_PI) + 512);
    if (radius < 512) radius = 512;
    int32_t max_radius = large ? 24576 : 8192;
    if (radius > max_radius) radius = max_radius;

    int32_t out_x = res.x, out_z = res.z;
    bool hit_edge = false;
    int32_t area = measure_1to1(cubiomes, res.x, res.z, radius, &out_x, &out_z, &hit_edge);

    if (hit_edge) {
        int32_t big_radius = radius * 3 / 2;
        if (big_radius > max_radius) big_radius = max_radius;
        if (big_radius > radius) {
            int32_t bx = res.x, bz = res.z;
            bool big_edge = false;
            int32_t big_area = measure_1to1(cubiomes, res.x, res.z, big_radius, &bx, &bz, &big_edge);
            if (big_area > area) {
                area = big_area;
                out_x = bx;
                out_z = bz;
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() * 1e-3;

    std::printf("Area:    %d square blocks\n", area);
    std::printf("Center:  (%d, %d)\n", out_x, out_z);
    std::printf("Time:    %.2fs\n", secs);

    cubiomes_free(cubiomes);
    return 0;
}
