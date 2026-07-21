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

int32_t measure_1to1(Cubiomes *cubiomes, int32_t cx, int32_t cz, int32_t radius, int32_t *out_x, int32_t *out_z, bool *hit_edge, bool from_center) {
    constexpr int32_t SEA_LEVEL = 62;
    int32_t gw = 2 * radius;
    int32_t ox = cx - radius;
    int32_t oz = cz - radius;

    // Scale-4 grid: world y=62 maps to y4 in {15,16} via Voronoi
    int32_t gw4 = gw / 4 + 4;
    int32_t ox4 = (ox - 8) / 4;
    int32_t oz4 = (oz - 8) / 4;

    // int8_t: just store is_mushroom boolean (4x less memory than int, better cache)
    std::vector<int8_t> biome4a((size_t)gw4 * gw4); // y4=15
    std::vector<int8_t> biome4b((size_t)gw4 * gw4); // y4=16

    unsigned nthreads = std::thread::hardware_concurrency();
    if (nthreads < 1) nthreads = 1;

    int cont_max = cubiomes_get_mushroom_cont_max(cubiomes);

    // Step 1: Build scale-4 grid (parallel) — continentalness early-exit skips 4/7 Perlin for ~95% of cells
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
                        int a, b;
                        cubiomes_sample_biome_2y(cubiomes, ox4 + i, oz4 + j, cont_max, &a, &b);
                        biome4a[(size_t)j * gw4 + i] = (a == 14);
                        biome4b[(size_t)j * gw4 + i] = (b == 14);
                    }
            });
        }
        for (auto &th : threads) th.join();
    }

    // Build dilated mask: mark scale-4 cells that are mushroom OR adjacent to one.
    // Voronoi can map a block to its naive cell or +1 in each axis, so if the naive
    // cell and all 8 neighbors are non-mushroom, the block definitely isn't mushroom.
    std::vector<int8_t> mush_mask((size_t)gw4 * gw4, 0);
    for (int32_t j = 0; j < gw4; j++) {
        for (int32_t i = 0; i < gw4; i++) {
            if (!biome4a[(size_t)j * gw4 + i] && !biome4b[(size_t)j * gw4 + i]) continue;
            for (int32_t dj = -1; dj <= 1; dj++) {
                int32_t nj = j + dj;
                if (nj < 0 || nj >= gw4) continue;
                for (int32_t di = -1; di <= 1; di++) {
                    int32_t ni = i + di;
                    if (ni < 0 || ni >= gw4) continue;
                    mush_mask[(size_t)nj * gw4 + ni] = 1;
                }
            }
        }
    }

    // Step 2: Voronoi lookup per block (parallel) — skip non-mushroom regions
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
                        // Naive scale-4 cell (what Voronoi bases its search on)
                        int32_t naive_x4 = (ox + i - 2) >> 2;
                        int32_t naive_z4 = (oz + j - 2) >> 2;
                        int32_t gi = naive_x4 - ox4;
                        int32_t gj = naive_z4 - oz4;
                        if (gi < 0 || gi >= gw4 || gj < 0 || gj >= gw4 || !mush_mask[(size_t)gj * gw4 + gi]) {
                            pass[(size_t)j * gw + i] = 0;
                            continue;
                        }
                        // Potential mushroom — do full Voronoi lookup
                        int32_t x4, y4, z4;
                        cubiomes_voronoi_map(cubiomes, ox + i, SEA_LEVEL, oz + j, &x4, &y4, &z4);
                        int32_t gx = x4 - ox4, gz = z4 - oz4;
                        if (gx < 0 || gx >= gw4 || gz < 0 || gz >= gw4) {
                            pass[(size_t)j * gw + i] = 0;
                        } else {
                            pass[(size_t)j * gw + i] = (y4 <= 15)
                                ? biome4a[(size_t)gz * gw4 + gx]
                                : biome4b[(size_t)gz * gw4 + gx];
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

    auto flood = [&](int32_t si, int32_t sj, int32_t &count, int32_t &bx, int32_t &bz, bool &edge) {
        pass[(size_t)sj * gw + si] = -1;
        stack.push_back({si, sj});

        int64_t sum_x = 0, sum_z = 0;
        count = 0;
        edge = false;

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

        bx = (int32_t)(sum_x / count);
        bz = (int32_t)(sum_z / count);
    };

    if (from_center) {
        // Flood only the component containing the grid center (world cx,cz).
        int32_t si = cx - ox, sj = cz - oz;
        if (si >= 0 && si < gw && sj >= 0 && sj < gw && pass[(size_t)sj * gw + si] == 1) {
            int32_t count = 0, bx = cx, bz = cz;
            bool edge = false;
            flood(si, sj, count, bx, bz, edge);
            best_count = count;
            best_x = bx;
            best_z = bz;
            best_hit_edge = edge;
        }
    } else {
        for (int32_t sj = 0; sj < gw; sj++) {
            for (int32_t si = 0; si < gw; si++) {
                if (pass[(size_t)sj * gw + si] != 1) continue;

                int32_t count = 0, bx = cx, bz = cz;
                bool edge = false;
                flood(si, sj, count, bx, bz, edge);

                if (count > best_count) {
                    best_count = count;
                    best_x = bx;
                    best_z = bz;
                    best_hit_edge = edge;
                }
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

// Fast parallel replacement for cubiomes_test_biome_centers(scale=4, tol=2).
// Generates a scale-4 boolean grid in parallel, flood fills to find the biome center.
static bool fast_find_center(Cubiomes *cubiomes, int32_t cx, int32_t cz, int32_t range,
                             int32_t min_size, int cont_max, int32_t tol, PosArea &res) {
    int32_t scale = 4;
    int32_t sx = range / scale;
    int32_t sz = range / scale;
    int32_t ox = (cx - range / 2) / scale;
    int32_t oz = (cz - range / 2) / scale;

    std::vector<int8_t> grid((size_t)sx * sz, 0);
    unsigned nthreads = std::thread::hardware_concurrency();
    if (nthreads < 1) nthreads = 1;

    // Parallel grid generation using fast biome sampler
    {
        std::vector<std::thread> threads;
        int32_t rows_per = (sz + (int32_t)nthreads - 1) / (int32_t)nthreads;
        for (unsigned t = 0; t < nthreads; t++) {
            int32_t r0 = (int32_t)t * rows_per;
            int32_t r1 = std::min(r0 + rows_per, sz);
            if (r0 >= r1) break;
            threads.emplace_back([&, r0, r1]() {
                for (int32_t j = r0; j < r1; j++)
                    for (int32_t i = 0; i < sx; i++) {
                        int a, b;
                        cubiomes_sample_biome_2y(cubiomes, ox + i, oz + j, cont_max, &a, &b);
                        grid[(size_t)j * sx + i] = (a == 14 || b == 14);
                    }
            });
        }
        for (auto &th : threads) th.join();
    }

    // Flood fill to find largest connected mushroom component
    struct PEntry { int32_t i, j; };
    std::vector<PEntry> pstack;
    pstack.reserve(1 << 16);

    int32_t best_n = 0;
    int64_t best_sx = 0, best_sz = 0;

    for (int32_t sj = 0; sj < sz; sj++) {
        for (int32_t si = 0; si < sx; si++) {
            if (grid[(size_t)sj * sx + si] != 1) continue;

            grid[(size_t)sj * sx + si] = -1;
            pstack.push_back({si, sj});

            int64_t sumx = 0, sumz = 0;
            int32_t n = 0;

            while (!pstack.empty()) {
                PEntry e = pstack.back();
                pstack.pop_back();
                sumx += ox + e.i;
                sumz += oz + e.j;
                n++;

                int32_t ni[4] = {e.i, e.i, e.i-1, e.i+1};
                int32_t nj[4] = {e.j-1, e.j+1, e.j, e.j};
                for (int k = 0; k < 4; k++) {
                    if (ni[k] < 0 || ni[k] >= sx || nj[k] < 0 || nj[k] >= sz) continue;
                    size_t nk = (size_t)nj[k] * sx + ni[k];
                    if (grid[nk] == 1) {
                        grid[nk] = -1;
                        pstack.push_back({ni[k], nj[k]});
                    }
                }
            }

            if (n > best_n) {
                best_n = n;
                best_sx = sumx;
                best_sz = sumz;
            }
        }
    }

    if (best_n == 0) return false;

    res.x = (int32_t)((best_sx / best_n + 0.5) * scale);
    res.z = (int32_t)((best_sz / best_n + 0.5) * scale);
    res.area = best_n * (scale * scale);
    return res.area >= min_size;
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

    int cont_max = cubiomes_get_mushroom_cont_max(cubiomes);
    PosArea res;
    if (!fast_find_center(cubiomes, input.x, input.z, range, min_size, cont_max, 2, res)) {
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

std::optional<CpuOutput> process_origin(Cubiomes *cubiomes, int32_t min_size, const GpuOutput &input) {
    cubiomes_apply_seed(cubiomes, input.seed);

    // Exact check: block (0, 62, 0) must be mushroom_fields (biome id 14).
    constexpr int32_t SEA_LEVEL = 62;
    constexpr int32_t MUSHROOM_FIELDS = 14;
    if (cubiomes_get_biome_at_block(cubiomes, 0, SEA_LEVEL, 0) != MUSHROOM_FIELDS) {
        return {};
    }

    if (fast_cpu) {
        // Cheap statistical pre-check only; does not guarantee the exact size.
        int32_t range = 12800 * (large_biomes ? 4 : 1);
        if (!cubiomes_test_monte_carlo(cubiomes, 0, 0, range, (min_size * 0.9), 0.999)) {
            return {};
        }
        return {{ input.seed, 0, 0, min_size }};
    }

    // Measure the connected mushroom component containing (0,0).
    int32_t max_radius = large_biomes ? 24576 : 8192;
    int32_t radius = 2048;

    int32_t exact_x = 0, exact_z = 0;
    bool hit_edge = false;
    int32_t exact_area = measure_1to1(cubiomes, 0, 0, radius, &exact_x, &exact_z, &hit_edge, true);

    // Grow the grid until the component no longer touches the edge or we hit the cap.
    while (hit_edge && radius < max_radius) {
        int32_t next = radius * 2;
        if (next > max_radius) next = max_radius;
        if (next == radius) break;
        radius = next;
        exact_x = 0; exact_z = 0;
        hit_edge = false;
        exact_area = measure_1to1(cubiomes, 0, 0, radius, &exact_x, &exact_z, &hit_edge, true);
    }

    if (exact_area >= min_size) {
        return {{ input.seed, exact_x, exact_z, exact_area }};
    }

    return {};
}

CpuThread::CpuThread(int id, int32_t min_size, bool origin, GpuOutputs &inputs, CpuOutputs &outputs) : Thread(), id(id), min_size(min_size), origin(origin), inputs(inputs), outputs(outputs) {
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

        const auto output = origin ? process_origin(cubiomes, min_size, input) : process(cubiomes, min_size, input);

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
