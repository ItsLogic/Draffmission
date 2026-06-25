#include "cpu.h"
#include "cubiomes.h"

#include <cinttypes>
#include <optional>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#include <algorithm>

bool fast_cpu = false;

int32_t measure_1to1(Cubiomes *cubiomes, int32_t cx, int32_t cz, int32_t radius, int32_t *out_x, int32_t *out_z, bool *hit_edge) {
    constexpr int32_t SEA_LEVEL = 62;
    int32_t gw = 2 * radius;
    int32_t ox = cx - radius;
    int32_t oz = cz - radius;

    // Scale-4 grid: world y=62 maps to y4 in {15,16} via Voronoi
    int32_t gw4 = gw / 4 + 4;
    int32_t ox4 = (ox - 8) / 4;
    int32_t oz4 = (oz - 8) / 4;

    std::vector<int> biome4a((size_t)gw4 * gw4); // y4=15
    std::vector<int> biome4b((size_t)gw4 * gw4); // y4=16

    unsigned nthreads = std::thread::hardware_concurrency();
    if (nthreads < 1) nthreads = 1;

    // Step 1: Build scale-4 grid (parallel) — 2 layers, shared climate params
    {
        std::vector<std::thread> threads;
        int32_t rows_per = (gw4 + (int32_t)nthreads - 1) / (int32_t)nthreads;
        for (unsigned t = 0; t < nthreads; t++) {
            int32_t r0 = (int32_t)t * rows_per;
            int32_t r1 = std::min(r0 + rows_per, gw4);
            if (r0 >= r1) break;
            threads.emplace_back([&, r0, r1]() {
                for (int32_t j = r0; j < r1; j++)
                    for (int32_t i = 0; i < gw4; i++) {
                        int32_t x4 = ox4 + i, z4 = oz4 + j;
                        biome4a[(size_t)j * gw4 + i] = cubiomes_sample_biome(cubiomes, x4, 15, z4);
                        biome4b[(size_t)j * gw4 + i] = cubiomes_sample_biome(cubiomes, x4, 16, z4);
                    }
            });
        }
        for (auto &th : threads) th.join();
    }

    // Step 2: Voronoi lookup per block (parallel)
    std::vector<int8_t> pass((size_t)gw * gw);
    {
        std::vector<std::thread> threads;
        int32_t rows_per = (gw + (int32_t)nthreads - 1) / (int32_t)nthreads;
        for (unsigned t = 0; t < nthreads; t++) {
            int32_t r0 = (int32_t)t * rows_per;
            int32_t r1 = std::min(r0 + rows_per, gw);
            if (r0 >= r1) break;
            threads.emplace_back([&, r0, r1]() {
                for (int32_t j = r0; j < r1; j++) {
                    for (int32_t i = 0; i < gw; i++) {
                        int32_t x4, y4, z4;
                        cubiomes_voronoi_map(cubiomes, ox + i, SEA_LEVEL, oz + j, &x4, &y4, &z4);
                        int32_t gx = x4 - ox4, gz = z4 - oz4;
                        if (gx < 0 || gx >= gw4 || gz < 0 || gz >= gw4) {
                            pass[(size_t)j * gw + i] = 0;
                        } else {
                            int biome = (y4 <= 15)
                                ? biome4a[(size_t)gz * gw4 + gx]
                                : biome4b[(size_t)gz * gw4 + gx];
                            pass[(size_t)j * gw + i] = (biome == 14);
                        }
                    }
                }
            });
        }
        for (auto &th : threads) th.join();
    }

    // Step 3: Find largest connected component (serial flood fill)
    struct Entry { int32_t i, j; };
    std::vector<Entry> stack;
    stack.reserve(1 << 16);

    const int32_t di[4] = {0, 0, -1, 1};
    const int32_t dj[4] = {-1, 1, 0, 0};

    int32_t best_count = 0;
    int32_t best_x = cx, best_z = cz;
    bool best_hit_edge = false;

    for (int32_t sj = 0; sj < gw; sj++) {
        for (int32_t si = 0; si < gw; si++) {
            if (pass[(size_t)sj * gw + si] != 1) continue;

            pass[(size_t)sj * gw + si] = -1;
            stack.push_back({si, sj});

            int64_t sum_x = 0, sum_z = 0;
            int32_t count = 0;
            bool edge = false;

            while (!stack.empty()) {
                Entry e = stack.back();
                stack.pop_back();
                sum_x += ox + e.i;
                sum_z += oz + e.j;
                count++;

                for (int k = 0; k < 4; k++) {
                    int32_t ni = e.i + di[k], nj = e.j + dj[k];
                    if (ni < 0 || ni >= gw || nj < 0 || nj >= gw) { edge = true; continue; }
                    if (pass[(size_t)nj * gw + ni] == 1) {
                        pass[(size_t)nj * gw + ni] = -1;
                        stack.push_back({ni, nj});
                    }
                }
            }

            if (count > best_count) {
                best_count = count;
                best_x = (int32_t)(sum_x / count);
                best_z = (int32_t)(sum_z / count);
                best_hit_edge = edge;
            }
        }
    }

    if (best_count > 0) {
        if (out_x) *out_x = best_x;
        if (out_z) *out_z = best_z;
    }
    if (hit_edge) *hit_edge = best_hit_edge;
    return best_count;
}

std::optional<CpuOutput> process(Cubiomes *cubiomes, int32_t min_size, const GpuOutput &input) {
    cubiomes_apply_seed(cubiomes, input.seed);

    int32_t range = 12800 * (large_biomes ? 4 : 1);

    if (!cubiomes_test_monte_carlo(cubiomes, input.x, input.z, range, (min_size * 0.9), 0.999)) {
        return {};
    }

    if (!cubiomes_test_biome_centers(cubiomes, input.x, input.z, range, min_size, 16, 4, nullptr)) {
        return {};
    }

    PosArea res;
    if (!cubiomes_test_biome_centers(cubiomes, input.x, input.z, range, min_size, 4, 2, &res)) {
        return {};
    }

    if (fast_cpu) {
        return {{ input.seed, res.x, res.z, res.area }};
    }

    int32_t exact_radius = (int32_t)(3.0 * std::sqrt((double)res.area / M_PI) + 512);
    if (exact_radius < 512) exact_radius = 512;
    int32_t max_radius = large_biomes ? 24576 : 8192;
    if (exact_radius > max_radius) exact_radius = max_radius;

    int32_t exact_x = res.x, exact_z = res.z;
    bool hit_edge = false;
    int32_t exact_area = measure_1to1(cubiomes, res.x, res.z, exact_radius, &exact_x, &exact_z, &hit_edge);

    // If the biome extends beyond the grid, retry with a larger radius
    if (hit_edge) {
        int32_t big_radius = exact_radius * 3 / 2;
        if (big_radius > max_radius) big_radius = max_radius;
        if (big_radius > exact_radius) {
            int32_t bx = res.x, bz = res.z;
            bool big_edge = false;
            int32_t big_area = measure_1to1(cubiomes, res.x, res.z, big_radius, &bx, &bz, &big_edge);
            if (big_area > exact_area) {
                exact_area = big_area;
                exact_x = bx;
                exact_z = bz;
            }
        }
    }

    if (exact_area >= min_size) {
        return {{ input.seed, exact_x, exact_z, exact_area }};
    }

    return {};
}

CpuThread::CpuThread(int id, int32_t min_size, GpuOutputs &inputs, CpuOutputs &outputs) : Thread(), id(id), min_size(min_size), inputs(inputs), outputs(outputs) {
    start();
}

void CpuThread::run() {
    std::printf("Started cpu thread %d\n", id);

    Cubiomes *cubiomes = cubiomes_create(large_biomes);

    while (!should_stop()) {
        GpuOutput input;
        {
            std::unique_lock lock(inputs.mutex);
            if (inputs.queue.empty()) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            input = inputs.queue.front();
            inputs.queue.pop();
        }

        const auto start = std::chrono::steady_clock::now();

        const auto output = process(cubiomes, min_size, input);

        const auto end = std::chrono::steady_clock::now();
        double time_total = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e-9;
        // std::printf("Cpu test took %.3f s\n", time_total);

        if (!output) continue;

        {
            std::lock_guard lock(outputs.mutex);
            outputs.queue.push(output.value());
        }
    }

    cubiomes_free(cubiomes);
}
