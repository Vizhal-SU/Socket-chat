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
#include <fcntl.h>

// --- Constants ---
inline constexpr const char* PORT = "8080";
inline constexpr int BACKLOG = 64;
inline constexpr int MAXDATASIZE = 512;

// --- RAII Socket Wrapper ---
// Manages the lifetime of a socket file descriptor.
struct Socket {
    int fd;
    explicit Socket(int fd = -1) : fd(fd) {}
    ~Socket() { if (fd != -1) ::close(fd); }

    // Disable copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Enable moving
    Socket(Socket&& other) noexcept : fd(other.fd) { other.fd = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd != -1) ::close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    int get() const { return fd; }
    explicit operator bool() const { return fd != -1; }
};


// Get sockaddr, IPv4 or IPv6:
inline void* get_in_addr(sockaddr* sa) {
    if (sa->sa_family == AF_INET) return &(((sockaddr_in*)sa)->sin_addr);
    return &(((sockaddr_in6*)sa)->sin6_addr);
}

// Send all data in a buffer, handling partial sends.
inline bool send_all(int fd, std::string_view sv) {
    size_t sent = 0;
    while (sent < sv.size()) {
        ssize_t n = ::send(fd, sv.data() + sent, sv.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // retry
            perror("send");
            return false;
        }
        if (n == 0) return false; // peer closed connection
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Creates and binds a listening socket on the given port.
inline Socket get_listener_socket(const char* port) {
    addrinfo hints{}, *servinfo, *p;
    int rv;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(nullptr, port, &hints, &servinfo)) != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
        return Socket{-1};
    }

    int listener_fd = -1;
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        listener_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener_fd < 0) continue;
        
        setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener_fd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener_fd);
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if (p == nullptr) {
        std::cerr << "server: failed to bind\n";
        return Socket{-1};
    }

    if (listen(listener_fd, BACKLOG) == -1) {
        perror("listen");
        return Socket{-1};
    }
    return Socket{listener_fd};
}

// Connects to a server at the given host and port.
inline Socket connect_to_server(const char* host, const char* port) {
    addrinfo hints{}, *servinfo, *p;
    int rv;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
        return Socket{-1};
    }

    int sockfd = -1;
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == nullptr) {
        std::cerr << "client: failed to connect\n";
        freeaddrinfo(servinfo);
        return Socket{-1};
    }
    
    char s[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    std::cout << "Connected to " << s << "\n";
    freeaddrinfo(servinfo);

    return Socket{sockfd};
}

// Sets a socket to non-blocking mode.
inline bool set_non_blocking(int fd) {
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        return false;
    }
    return true;
}