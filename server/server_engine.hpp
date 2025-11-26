#pragma once
#include "network_utils.hpp"
#include <vector>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <array>

// maximum number of events to get from epoll_wait at once
constexpr int MAX_EVENTS = 64;

// This is the generic Server Engine, templated on a "LogicHandler".
// It is a self-contained component that owns all networking resources.
template<typename LogicHandler>
class ServerEngine {
public:
    // Public method for the LogicHandler to call back into the engine
    void send_to_client(int fd, std::string_view msg) {
        send_all(fd, msg);
    }

    void run() {
        std::cout << "Server (epoll) listening on port " << PORT << "...\n";
        std::array<epoll_event, MAX_EVENTS> events;

        while (true) {
            int num_events = ::epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, -1);
            if (num_events < 0) {
                if (errno == EINTR) continue; // Interrupted by a signal, retry.
                perror("epoll_wait");
                break;
            }

            // Iterate ONLY through the sockets that are ready
            for (int i = 0; i < num_events; ++i) {
                int fd = events[i].data.fd;
                if (fd == listener_.get()) {
                    handle_new_connection();
                } else {
                    handle_client_data(fd);
                }
            }
        }
    }

protected:
    // The constructor is protected. The derived class must call it.
    // Initializes the engine's own state (listener and epoll instance).
    explicit ServerEngine(const std::string& port) 
        : listener_(get_listener_socket(port.c_str())),
          epoll_fd_(::epoll_create1(0)) {

        if (!listener_) {
            throw std::runtime_error("Failed to initialize listener socket.");
        }
        if (epoll_fd_ < 0) {
            throw std::runtime_error("Failed to create epoll instance.");
        }

        // Add the listener socket to the epoll watchlist
        epoll_add(listener_.get());
    }

private:
    void handle_new_connection() {
        int client_fd = ::accept(listener_.get(), nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept");
            return;
        }
        if (!set_non_blocking(client_fd)) {
            ::close(client_fd);
            return;
        }
        
        // Add the new client socket to the epoll watchlist
        epoll_add(client_fd);
        
        // CRTP MAGIC: Delegate to the derived LogicHandler
        static_cast<LogicHandler*>(this)->on_connect(client_fd);
    }

    void handle_client_data(int client_fd) {
        char buf[MAXDATASIZE];
        ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (n <= 0) {
            // Client disconnected or error
            epoll_del(client_fd);
            ::close(client_fd);
            static_cast<LogicHandler*>(this)->on_disconnect(client_fd);
        } else {
            buf[n] = '\0';
            // CRTP MAGIC: Delegate message handling to the derived LogicHandler
            static_cast<LogicHandler*>(this)->on_message(client_fd, std::string(buf));
        }
    }

    // Helper to add a file descriptor to the epoll watchlist
    void epoll_add(int fd) {
        epoll_event event;
        event.events = EPOLLIN; // Watch for readable events
        event.data.fd = fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
            perror("epoll_ctl ADD failed");
        }
    }

    // Helper to remove a file descriptor from the epoll watchlist
    void epoll_del(int fd) {
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            perror("epoll_ctl DEL failed");
        }
    }

    // State now correctly lives inside the engine
    Socket listener_;
    int epoll_fd_;
};