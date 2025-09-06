#include "utils.hpp"
#include <atomic>
#include <iostream>
#include <string>

#include <readline/history.h>
#include <readline/readline.h>
#include <sys/select.h>
#include <unistd.h> // For STDIN_FILENO

// Global state for the callback and main loop
static std::atomic<bool> running(true);
static int global_sockfd = -1;

// This function is called by Readline when the user presses Enter
void line_handler(char* line_c) {
    if (line_c == nullptr) { // User pressed Ctrl+D
        running = false;
        return;
    }
    
    if (line_c[0] != '\0') {
        add_history(line_c);
        if (!send_all(global_sockfd, std::string(line_c))) {
            running = false;
        }
    }
    
    free(line_c);
}

int main() {
    Socket sock = connect_to_server("127.0.0.1", PORT);
    if (!sock) return 1;
    global_sockfd = sock.get();

    std::string name;
    std::cout << "Enter your name: ";
    std::getline(std::cin, name);
    if (!send_all(global_sockfd, name + "\n")) {
        std::cerr << "Failed to send handshake.\n";
        return 1;
    }

    rl_callback_handler_install(">> ", line_handler);

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(global_sockfd, &readfds);

        if (select(global_sockfd + 1, &readfds, nullptr, nullptr, nullptr) < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            rl_callback_read_char();
        }

        if (FD_ISSET(global_sockfd, &readfds)) {
            char buf[MAXDATASIZE];
            ssize_t n = ::recv(global_sockfd, buf, sizeof(buf) - 1, 0);

            if (n <= 0) {
                if (running) {
                    // Save user's line, print disconnect, then restore
                    char *saved_line = rl_copy_text(0, rl_end);
                    int saved_point = rl_point;
                    std::cout << "\r\x1b[K[disconnected]\n" << std::flush;
                    rl_replace_line(saved_line, 0);
                    rl_point = saved_point;
                    rl_forced_update_display();
                    free(saved_line);
                }
                running = false;
                break;
            }
            buf[n] = '\0';
            std::string msg(buf);
            if (!msg.empty() && msg.back() == '\n') msg.pop_back();

            // --- UNIFIED MESSAGE HANDLING LOGIC ---
            
            // 1. Save what the user is currently typing
            char *saved_line = rl_copy_text(0, rl_end);
            int saved_point = rl_point;

            // 2. Clear the prompt line and print the new message
            std::cout << "\r\x1b[K";
            
            // Check if it's our own message and format it
            const std::string my_tag = "[" + name + "]";
            if (msg.find(my_tag, 0) != std::string::npos) {
                msg.replace(msg.find(my_tag, 0), my_tag.length(), "\t\tYou: ");
                std::cout << "\x1b[A\x1b[2K";
            }
            std::cout << msg << std::endl;

            // 3. Restore the user's saved text into Readline's buffer
            rl_replace_line(saved_line, 0);
            rl_point = saved_point;

            // 4. Tell Readline to redraw the prompt and the restored text
            rl_forced_update_display();

            free(saved_line);
        }
    }
    
    rl_callback_handler_remove();
    std::cout << "\nGoodbye!\n";
    return 0;
}