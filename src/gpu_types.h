// gpu_types.h — Shared types for ABI compatibility
#pragma once
#include <cstdint>
#include <cuda_runtime.h>

struct alignas(16) ImprovedNoise { uint8_t p[256]; float xo, yo, zo, pad; };
struct GradDotTable { float x[16], y[16], z[16]; };
struct SeedPos { uint32_t seed_index; int32_t x, z; };
struct DeviceBuffer {
    void *data; size_t size;
    DeviceBuffer(size_t s) : size(s) { cudaMalloc(&data, s); }
    ~DeviceBuffer() { cudaFree(data); }
};
template<typename T> struct OutputBuffer {
    T *data; uint32_t *len; uint32_t max_len;
    OutputBuffer(T *d, uint32_t *l, uint32_t m) : data(d), len(l), max_len(m) {}
    OutputBuffer(const DeviceBuffer &b, uint32_t *l) : data((T*)b.data), len(l), max_len(b.size/sizeof(T)) {}
    OutputBuffer(const OutputBuffer<T> &o) : data(o.data), len(o.len), max_len(o.max_len) {}
};
template<typename T> struct InputBuffer {
    const T *data; const uint32_t *len;
    InputBuffer(const T *d, const uint32_t *l) : data(d), len(l) {}
    InputBuffer(const OutputBuffer<T> &b) : data(b.data), len(b.len) {}
    InputBuffer(const InputBuffer<T> &o) : data(o.data), len(o.len) {}
};
namespace KernelSeed1 {
    constexpr uint32_t threads_per_run = UINT64_C(1) << 16;
    struct Result {
        ImprovedNoise continentalness_0A, continentalness_0B, continentalness_1A, continentalness_1B,
        continentalness_2A, continentalness_2B, continentalness_3A, continentalness_3B,
        continentalness_4A, continentalness_4B, continentalness_5A, continentalness_5B,
        continentalness_6A, continentalness_6B, continentalness_7A, continentalness_7B,
        continentalness_8A, continentalness_8B;
    };
}
