#include "client.hpp"
#include <iostream>
#include <stdexcept>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/select.h>
#include <unistd.h>

// Initialize static pointer
ChatClient* ChatClient::current_instance_ = nullptr;

ChatClient::ChatClient(const std::string& host, const std::string& port) 
    : sock_(connect_to_server(host.c_str(), port.c_str())) {
    if (!sock_) {
        throw std::runtime_error("Failed to connect to server");
    }
    current_instance_ = this;
}

void ChatClient::run() {
    std::cout << "Enter your name: ";
    std::getline(std::cin, name_);
    if (!send_all(sock_.get(), name_ + "\n")) {
        std::cerr << "Failed to send handshake.\n";
        return;
    }

    setup_readline();
    event_loop();
    cleanup_readline();

    std::cout << "\nGoodbye!\n";
}

void ChatClient::setup_readline() {
    rl_callback_handler_install(">> ", ChatClient::line_handler);
}

void ChatClient::cleanup_readline() {
    rl_callback_handler_remove();
}

void ChatClient::event_loop() {
    while (running_) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock_.get(), &readfds);

        if (select(sock_.get() + 1, &readfds, nullptr, nullptr, nullptr) < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            handle_user_input();
        }

        if (FD_ISSET(sock_.get(), &readfds)) {
            handle_network_message();
        }
    }
}

void ChatClient::handle_user_input() {
    rl_callback_read_char();
}

void ChatClient::handle_network_message() {
    char buf[MAXDATASIZE];
    ssize_t n = ::recv(sock_.get(), buf, sizeof(buf) - 1, 0);

    if (n <= 0) { // Handle disconnect
        if (running_) {
            std::cout << "\r\x1b[K[disconnected]\n" << std::flush;
            rl_redisplay(); // Just redisplay here, no need for full save/restore
        }
        running_ = false;
        return;
    }
    buf[n] = '\0';
    std::string msg(buf);
    
    // 1. Save what the user is currently typing
    char *saved_line = rl_copy_text(0, rl_end);
    int saved_point = rl_point;

    // 2. Clear the prompt line visually
    std::cout << "\r\x1b[K";
    
    // 3. Format the message (handle "You:" case)
    const std::string my_tag = "[" + name_ + "]";
    if (msg.find(my_tag) != std::string::npos) {
        msg.replace(msg.find(my_tag), my_tag.length(), "You:");
        std::cout << "\x1b[A\x1b[2K";
    }
    
    // 4. Print the final, formatted message and a newline
    std::cout << msg << std::flush;

    // 5. Restore the user's saved text into Readline's buffer
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;

    // 6. Tell Readline to redraw the prompt and the restored text
    rl_forced_update_display();

    free(saved_line);
}

void ChatClient::line_handler(char* line) {     // static (c-style) fn as required by readline
    if (line == nullptr) { // Ctrl+D
        current_instance_->running_ = false;
        return;
    }
    
    if (line[0] != '\0') {
        add_history(line);
        if (!send_all(current_instance_->sock_.get(), std::string(line) + "\n")) {
            current_instance_->running_ = false;
        }
    }
    
    free(line);
}

int main() {
    try {
        ChatClient client("127.0.0.1", PORT);
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}