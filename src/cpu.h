#pragma once

#include "common.h"
#include <optional>

struct Cubiomes;

struct CpuThread: Thread<CpuThread> {
    int id;
    int32_t min_size;
    GpuOutputs &inputs;
    CpuOutputs &outputs;

    CpuThread(int id, int32_t min_size, GpuOutputs &inputs, CpuOutputs &outputs);

    void run();
};

extern bool fast_cpu;

int32_t measure_1to1(Cubiomes *cubiomes, int32_t cx, int32_t cz, int32_t radius, int32_t *out_x, int32_t *out_z, bool *hit_edge);
std::optional<CpuOutput> process(Cubiomes *cubiomes, int32_t min_size, const GpuOutput &input);