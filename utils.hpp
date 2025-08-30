#pragma once
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

inline constexpr const char* PORT = "3490";
inline constexpr int BACKLOG = 64;
inline constexpr int MAXDATASIZE = 1024;

// RAII wrapper for a socket fd
struct Socket {
    int fd;
    explicit Socket(int fd = -1) : fd(fd) {}    // Constructor
    ~Socket() { if (fd != -1) close(fd); }  // Destructor

    // Copy
    Socket(const Socket&) = delete;     // No copy -> Socket s1(5);
    Socket& operator=(const Socket&) = delete;  // No assignment -> s1 = s2;

    // Move
    Socket(Socket&& other) noexcept : fd(other.fd) { other.fd = -1; }   // Move -> Socket s2(std::move(s1));
        // noexcept- tells that no exception will be raised. If not specified, copy will be used
    Socket& operator=(Socket&& other) noexcept {    // Move assignment -> s2 = std::move(s1);
        if (this != &other) {
            if (fd != -1) close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    int get() const { return fd; }
    explicit operator bool() const { return fd != -1; }
    void reset(int f = -1) {
        if (fd != -1) ::close(fd);
        fd = f;
    }
};

// IPv4/IPv6 helper
inline void* get_in_addr(sockaddr* sa) {
    if (sa->sa_family == AF_INET) return &(((sockaddr_in*)sa)->sin_addr);
    return &(((sockaddr_in6*)sa)->sin6_addr);
}

// Best-effort send all bytes in [data, data+len)
inline bool send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // retry
            return false;
        }
        if (n == 0) return false; // peer closed
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Convenience for sending std::string_view
inline bool send_all(int fd, std::string_view sv) {
    return send_all(fd, sv.data(), sv.size());
}

extern std::unordered_map<int, std::string> client_names;
