#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

extern "C" {
#include "generator.h"
#include "biomes.h"
#include "util.h"
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --seed <seed> --x <x> --z <z> [options]\n"
        "  --seed <n>      World seed (required)\n"
        "  --x <n>         Island center X (required)\n"
        "  --z <n>         Island center Z (required)\n"
        "  --scale <n>     Biome scale: 1=block, 4=biome (default: 4)\n"
        "  --width <n>     Output width in pixels (default: 512)\n"
        "  --height <n>    Output height in pixels (default: 512)\n"
        "  --y <n>         Y coordinate at scale (default: 15 for scale 4, 62 for scale 1)\n"
        "  --output <path> Output PPM file path (required)\n"
        "  --large-biomes  Use large biomes flag\n"
        "  --zoom <n>      Area coverage multiplier (default: 1)\n"
        "  --sample        Point-sample at strided positions (fast, for thumbnails)\n",
        prog);
}

int main(int argc, char **argv) {
    int64_t worldseed = 0;
    int32_t cx = 0, cz = 0;
    int scale = 4;
    int width = 512;
    int height = 512;
    int y = -1;
    int large_biomes = 0;
    int zoom = 1;
    int sample_mode = 0;
    const char *output_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i+1 < argc) {
            worldseed = strtoll(argv[++i], nullptr, 10);
        } else if (!strcmp(argv[i], "--x") && i+1 < argc) {
            cx = strtol(argv[++i], nullptr, 10);
        } else if (!strcmp(argv[i], "--z") && i+1 < argc) {
            cz = strtol(argv[++i], nullptr, 10);
        } else if (!strcmp(argv[i], "--scale") && i+1 < argc) {
            scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--width") && i+1 < argc) {
            width = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--height") && i+1 < argc) {
            height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--y") && i+1 < argc) {
            y = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--output") && i+1 < argc) {
            output_path = argv[++i];
        } else if (!strcmp(argv[i], "--large-biomes")) {
            large_biomes = 1;
        } else if (!strcmp(argv[i], "--zoom") && i+1 < argc) {
            zoom = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--sample")) {
            sample_mode = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
    }

    if (!output_path) {
        fprintf(stderr, "Error: --output is required\n");
        usage(argv[0]);
        return 1;
    }

    if (y < 0) {
        y = (scale == 1) ? 62 : 15;
    }

    unsigned char colors[256][3];
    initBiomeColors(colors);

    Generator g;
    setupGenerator(&g, MC_NEWEST, large_biomes ? LARGE_BIOMES : 0);
    applySeed(&g, 0, (uint64_t)worldseed);

    if (sample_mode) {
        int stride = scale * zoom;
        int block_half_w = (width * stride) / 2;
        int block_half_h = (height * stride) / 2;
        int start_x = cx - block_half_w + stride / 2;
        int start_z = cz - block_half_h + stride / 2;

        unsigned char *pixels = (unsigned char*)malloc((size_t)width * height * 3);
        if (!pixels) {
            fprintf(stderr, "Error: malloc failed\n");
            return 1;
        }

        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++) {
                int wx = start_x + i * stride;
                int wz = start_z + j * stride;
                int id = getBiomeAt(&g, scale, wx / scale, y, wz / scale);
                int idx = (j * width + i) * 3;
                if (id >= 0 && id < 256) {
                    pixels[idx]     = colors[id][0];
                    pixels[idx + 1] = colors[id][1];
                    pixels[idx + 2] = colors[id][2];
                } else {
                    pixels[idx] = pixels[idx + 1] = pixels[idx + 2] = 0;
                }
            }
        }

        int rc = savePPM(output_path, pixels, width, height);
        free(pixels);
        if (rc != 0) {
            fprintf(stderr, "Error: savePPM failed (rc=%d)\n", rc);
            return 1;
        }
        fprintf(stderr, "Sampled %dx%d (stride=%d) -> %s\n", width, height, stride, output_path);
        return 0;
    }

    int block_half_w = (width * scale * zoom) / 2;
    int block_half_h = (height * scale * zoom) / 2;
    int sx_start = (cx - block_half_w);
    int sz_start = (cz - block_half_h);
    int x4 = sx_start / scale;
    int z4 = sz_start / scale;
    int eff_w = width * zoom;
    int eff_h = height * zoom;
    if (eff_w > 4096) eff_w = 4096;
    if (eff_h > 4096) eff_h = 4096;

    Range r = {};
    r.scale = scale;
    r.x = x4;
    r.z = z4;
    r.sx = eff_w;
    r.sz = eff_h;
    r.y = y;
    r.sy = 1;

    int *biomes = allocCache(&g, r);
    if (!biomes) {
        fprintf(stderr, "Error: allocCache failed for %dx%d\n", eff_w, eff_h);
        return 1;
    }

    if (genBiomes(&g, biomes, r) != 0) {
        fprintf(stderr, "Error: genBiomes failed\n");
        free(biomes);
        return 1;
    }

    if (zoom > 1) {
        unsigned char *pixels = (unsigned char*)malloc((size_t)width * height * 3);
        if (!pixels) {
            fprintf(stderr, "Error: malloc failed\n");
            free(biomes);
            return 1;
        }

        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++) {
                int sr = 0, sg = 0, sb = 0;
                int count = 0;
                for (int dj = 0; dj < zoom; dj++) {
                    for (int di = 0; di < zoom; di++) {
                        int si = i * zoom + di;
                        int sj = j * zoom + dj;
                        if (si < eff_w && sj < eff_h) {
                            int id = biomes[sj * eff_w + si];
                            if (id >= 0 && id < 256) {
                                sr += colors[id][0];
                                sg += colors[id][1];
                                sb += colors[id][2];
                            }
                            count++;
                        }
                    }
                }
                if (count > 0) {
                    int idx = (j * width + i) * 3;
                    pixels[idx]     = sr / count;
                    pixels[idx + 1] = sg / count;
                    pixels[idx + 2] = sb / count;
                }
            }
        }
        free(biomes);

        int rc = savePPM(output_path, pixels, width, height);
        free(pixels);
        if (rc != 0) {
            fprintf(stderr, "Error: savePPM failed (rc=%d)\n", rc);
            return 1;
        }
    } else {
        unsigned char *pixels = (unsigned char*)malloc((size_t)eff_w * eff_h * 3);
        if (!pixels) {
            fprintf(stderr, "Error: malloc failed\n");
            free(biomes);
            return 1;
        }
        biomesToImage(pixels, colors, biomes, eff_w, eff_h, 1, 1);
        free(biomes);

        int rc = savePPM(output_path, pixels, eff_w, eff_h);
        free(pixels);
        if (rc != 0) {
            fprintf(stderr, "Error: savePPM failed (rc=%d)\n", rc);
            return 1;
        }
    }

    fprintf(stderr, "Rendered %dx%d at scale %d -> %s\n", width, height, scale, output_path);
    return 0;
}
