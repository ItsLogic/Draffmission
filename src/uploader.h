#pragma once

#include "common.h"
#include <string>
#include <queue>

struct UploadItem {
    uint64_t seed;
    int32_t x;
    int32_t z;
    int32_t size;
    std::string mode;
};

bool check_server_alive(const std::string &host, const std::string &port);

struct UploaderThread : Thread<UploaderThread> {
    std::string host;
    std::string port;
    std::queue<UploadItem> queue;
    std::mutex mutex;

    UploaderThread(std::string host_, std::string port_) : host(std::move(host_)), port(std::move(port_)) {
        start();
    }

    void enqueue(uint64_t seed, int32_t x, int32_t z, int32_t size, const char *mode) {
        std::lock_guard lock(mutex);
        queue.push({seed, x, z, size, mode});
    }

    void run();
};
