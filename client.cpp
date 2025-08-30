// client.cpp
// Multi-client chat client
// Features:
//  - Ask user for name, send it immediately (handshake)
//  - Reader thread listens for messages
//  - Replace own [name] with "You:"
//  - Messages appear color-coded as sent by server

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Background thread: listens for messages from server
void socket_reader(int sockfd, const std::string& myname, std::atomic<bool>& running) {
    char buf[512];
    while (running) {
        ssize_t n = ::recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            std::cout << "\n[disconnected]\n";
            running = false;
            break;
        }
        buf[n] = '\0';
        std::string msg(buf);

        // If server sent our own name, replace with "You:"
        std::string tag = "[" + myname + "]";
        if (msg.find(tag) == 0) {
            msg.replace(0, tag.size(), "You:");
        }

        std::cout << "\r" << msg << "\n\t>> " << std::flush;
    }
}

int main() {
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (::connect(sockfd, (sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect");
        return 1;
    }

    std::cout << "Enter your name: ";
    std::string name;
    std::getline(std::cin, name);

    // Send name as handshake
    std::string hello = name + "\n";
    ::send(sockfd, hello.c_str(), hello.size(), 0);

    std::atomic<bool> running(true);
    std::thread reader(socket_reader, sockfd, name, std::ref(running));

    // Main loop: read stdin and send to server
    std::string line;
    while (running && std::getline(std::cin, line)) {
        line += "\n";
        ::send(sockfd, line.c_str(), line.size(), 0);
        std::cout << "\t>> " << std::flush;
    }

    running = false;
    ::close(sockfd);
    reader.join();

    return 0;
}
