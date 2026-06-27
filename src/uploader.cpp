#include "uploader.h"

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <string>
#include <chrono>
#include <thread>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

bool check_server_alive(const std::string &host, const std::string &port) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || !result) return false;

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) { freeaddrinfo(result); return false; }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool ok = connect(fd, result->ai_addr, result->ai_addrlen) == 0;
    close(fd);
    freeaddrinfo(result);
    return ok;
}

static bool http_post(const std::string &host, const std::string &port, const std::string &path) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        std::fprintf(stderr, "[uploader] getaddrinfo failed for %s:%s: %s\n", host.c_str(), port.c_str(), gai_strerror(rc));
        return false;
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        std::fprintf(stderr, "[uploader] socket creation failed\n");
        return false;
    }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        std::fprintf(stderr, "[uploader] connect to %s:%s failed\n", host.c_str(), port.c_str());
        close(fd);
        freeaddrinfo(result);
        return false;
    }
    freeaddrinfo(result);

    std::string request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + ":" + port + "\r\n";
    request += "Connection: close\r\n";
    request += "Content-Length: 0\r\n";
    request += "\r\n";

    ssize_t sent = send(fd, request.c_str(), request.size(), 0);
    if (sent < 0) {
        std::fprintf(stderr, "[uploader] send failed\n");
        close(fd);
        return false;
    }

    char buf[512];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    close(fd);

    if (n <= 0) {
        std::fprintf(stderr, "[uploader] no response\n");
        return false;
    }

    buf[n] = '\0';
    if (std::strncmp(buf, "HTTP/1.1 200", 12) != 0 && std::strncmp(buf, "HTTP/1.0 200", 12) != 0) {
        std::fprintf(stderr, "[uploader] bad response: %.80s\n", buf);
        return false;
    }

    return true;
}

void UploaderThread::run() {
    while (!should_stop()) {
        UploadItem item;
        bool has_item = false;
        {
            std::lock_guard lock(mutex);
            if (!queue.empty()) {
                item = queue.front();
                queue.pop();
                has_item = true;
            }
        }

        if (has_item) {
            char path[512];
            std::snprintf(path, sizeof(path),
                "/api/seeds/single?seed=%" PRIu64 "&x=%" PRIi32 "&z=%" PRIi32 "&size=%" PRIi32 "&mode=%s",
                item.seed, item.x, item.z, item.size, item.mode.c_str());

            if (http_post(host, port, path)) {
                std::printf("[uploader] uploaded seed %" PRIu64 "\n", item.seed);
            } else {
                std::fprintf(stderr, "[uploader] failed seed %" PRIu64 ", will retry\n", item.seed);
                std::lock_guard lock(mutex);
                queue.push(item);
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
