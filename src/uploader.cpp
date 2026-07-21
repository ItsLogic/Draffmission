#include "uploader.h"

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <string>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define CLOSESOCKET closesocket
#define SOCKET_VALID(fd) ((fd) != INVALID_SOCKET)
static bool wsa_init() {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
}
static void wsa_cleanup() { WSACleanup(); }
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSESOCKET close
#define SOCKET_VALID(fd) ((fd) >= 0)
static bool wsa_init() { return true; }
static void wsa_cleanup() {}
#endif

bool check_server_alive(const std::string &host, const std::string &port) {
    if (!wsa_init()) return false;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || !result) return false;

    socket_t fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (!SOCKET_VALID(fd)) { freeaddrinfo(result); return false; }

#ifdef _WIN32
    DWORD tv = 1000;
#else
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
#endif
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    bool ok = connect(fd, result->ai_addr, result->ai_addrlen) == 0;
    CLOSESOCKET(fd);
    freeaddrinfo(result);
    return ok;
}

static bool http_post(const std::string &host, const std::string &port, const std::string &path) {
    if (!wsa_init()) return false;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        std::fprintf(stderr, "[uploader] getaddrinfo failed for %s:%s: %s\n", host.c_str(), port.c_str(), gai_strerror(rc));
        return false;
    }

    socket_t fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (!SOCKET_VALID(fd)) {
        freeaddrinfo(result);
        std::fprintf(stderr, "[uploader] socket creation failed\n");
        return false;
    }

#ifdef _WIN32
    DWORD tv = 5000;
#else
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
#endif
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        std::fprintf(stderr, "[uploader] connect to %s:%s failed\n", host.c_str(), port.c_str());
        CLOSESOCKET(fd);
        freeaddrinfo(result);
        return false;
    }
    freeaddrinfo(result);

    std::string request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + ":" + port + "\r\n";
    request += "Connection: close\r\n";
    request += "Content-Length: 0\r\n";
    request += "\r\n";

    int sent = send(fd, request.c_str(), (int)request.size(), 0);
    if (sent < 0) {
        std::fprintf(stderr, "[uploader] send failed\n");
        CLOSESOCKET(fd);
        return false;
    }

    char buf[512];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    CLOSESOCKET(fd);

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
    wsa_init();
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
