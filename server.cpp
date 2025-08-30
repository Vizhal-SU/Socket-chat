// server.cpp
// A simple multi-client chat server using poll()
// Features:
//  - Clients send their name immediately on connect
//  - Server assigns them a color (rotating palette)
//  - Server stores a map of fd -> {name, color}
//  - Messages are broadcast with [name] in color

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

// Color palette for each client
const std::vector<std::string> COLORS = {
    "\033[31m", "\033[32m", "\033[33m",
    "\033[34m", "\033[35m", "\033[36m"
};
const std::string RESET = "\033[0m";

// Client metadata
struct ClientInfo {
    std::string name;
    std::string color;
};

// Shared registry of all connected clients
struct ClientRegistry {
    std::mutex mtx;
    std::map<int, ClientInfo> clients; // fd -> info
};

// Send message to all connected clients
void broadcast(ClientRegistry& reg, const std::string& msg, int sender_fd = -1) {
    std::lock_guard<std::mutex> lock(reg.mtx);
    for (auto& [fd, info] : reg.clients) {
        if (fd != sender_fd) {
            ::send(fd, msg.c_str(), msg.size(), 0);
        }
    }
}

int main() {
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);  // listen on port 8080
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(listener, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (::listen(listener, 10) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Server listening on port 8080...\n";

    ClientRegistry registry;
    std::vector<pollfd> fds;
    fds.push_back({listener, POLLIN, 0});

    while (true) {
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            perror("poll");
            break;
        }

        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == listener) {
                    // New connection
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
                    int client_fd = ::accept(listener, (sockaddr*)&client_addr, &len);
                    if (client_fd < 0) continue;

                    fds.push_back({client_fd, POLLIN, 0});

                    // First thing: read client name
                    char buf[128];
                    ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
                    if (n <= 0) {
                        ::close(client_fd);
                        fds.pop_back();
                        continue;
                    }
                    buf[n] = '\0';
                    std::string name(buf);
                    if (!name.empty() && name.back() == '\n') name.pop_back();

                    std::string color = COLORS[client_fd % COLORS.size()];

                    {
                        std::lock_guard<std::mutex> lk(registry.mtx);
                        registry.clients[client_fd] = {name, color};
                    }

                    std::string join_msg = color + "[server] " + RESET + name + " joined\n";
                    broadcast(registry, join_msg, -1);

                    std::cout << name << " connected.\n";
                } else {
                    // Message from existing client
                    char buf[512];
                    ssize_t n = ::recv(fds[i].fd, buf, sizeof(buf) - 1, 0);
                    if (n <= 0) {
                        // client disconnected
                        int fd = fds[i].fd;
                        std::string name;
                        {
                            std::lock_guard<std::mutex> lk(registry.mtx);
                            name = registry.clients[fd].name;
                            registry.clients.erase(fd);
                        }
                        std::string msg = "[server] " + name + " left\n";
                        broadcast(registry, msg, -1);

                        ::close(fd);
                        fds.erase(fds.begin() + i);
                        i--;
                    } else {
                        buf[n] = '\0';
                        int fd = fds[i].fd;

                        std::string name, color;
                        {
                            std::lock_guard<std::mutex> lk(registry.mtx);
                            name = registry.clients[fd].name;
                            color = registry.clients[fd].color;
                        }

                        std::string line = color + "[" + name + "] " + RESET + std::string(buf);
                        broadcast(registry, line, fd);
                        std::cout << line;
                    }
                }
            }
        }
    }

    return 0;
}
