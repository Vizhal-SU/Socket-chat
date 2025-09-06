#include "utils.hpp"
#include <vector>
#include <map>
#include <mutex>
#include <poll.h>

const std::vector<std::string> COLORS = {"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[36m"};
const std::string RESET = "\033[0m";

struct ClientInfo {
    std::string name;
    std::string color;
};

struct ClientRegistry {
    std::mutex mtx;
    std::map<int, ClientInfo> clients; // fd -> info
};

void broadcast(ClientRegistry& reg, std::string_view msg, int sender_fd) {
    std::lock_guard<std::mutex> lock(reg.mtx);
    for (const auto& [fd, info] : reg.clients) {
        // if (fd != sender_fd) {
            send_all(fd, msg);
        // }
    }
}

int main() {
    Socket listener = get_listener_socket(PORT);
    if (!listener) {
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "...\n";

    ClientRegistry registry;
    std::vector<pollfd> fds;
    fds.push_back({listener.get(), POLLIN, 0});

    while (true) {
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            perror("poll");
            break;
        }

        for (size_t i = 0; i < fds.size(); i++) {
            if (!(fds[i].revents & POLLIN)) continue;

            if (fds[i].fd == listener.get()) { // New connection
                int client_fd = ::accept(listener.get(), nullptr, nullptr);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }
                fds.push_back({client_fd, POLLIN, 0});

                char buf[128];
                ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) { // Handshake failed
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
                std::string join_msg = "\n[server] " + name + " joined\n\n";
                broadcast(registry, join_msg, -1);
                std::cout << name << " connected.\n";

            } else { // Message from existing client
                int client_fd = fds[i].fd;
                char buf[MAXDATASIZE];
                ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);

                if (n <= 0) { // Client disconnected
                    std::string name;
                    {
                        std::lock_guard<std::mutex> lk(registry.mtx);
                        if (registry.clients.count(client_fd)) {
                            name = registry.clients.at(client_fd).name;
                            registry.clients.erase(client_fd);
                        }
                    }
                    if (!name.empty()) {
                        std::string msg = "\n[server] " + name + " left\n\n";
                        broadcast(registry, msg, -1);
                        std::cout << name << " disconnected.\n";
                    }
                    ::close(client_fd);
                    fds.erase(fds.begin() + i--);
                } else { // Broadcast message
                    buf[n] = '\0';
                    std::string name, color;
                    {
                        std::lock_guard<std::mutex> lk(registry.mtx);
                        const auto& info = registry.clients.at(client_fd);
                        name = info.name;
                        color = info.color;
                    }
                    std::string line = color + "[" + name + "] " + RESET + std::string(buf)+ '\n';
                    broadcast(registry, line, client_fd);
                    std::cout << line; // Log chat to server console
                }
            }
        }
    }
    return 0; // listener socket closed by RAII
}