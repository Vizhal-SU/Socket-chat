#pragma once

#include "network_utils.hpp"
#include <string>
#include <atomic>

class ChatClient {
public:
    ChatClient(const std::string& host, const std::string& port);
    void run();

private:
    // --- Member Variables (State) ---
    Socket sock_;
    std::string name_;
    std::atomic<bool> running_{true};

    // Static pointer to the current instance, used as a bridge for the C-style callback.
    static ChatClient* current_instance_;

    // --- Private Helper Functions ---
    void setup_readline();
    void event_loop();
    void cleanup_readline();
    
    // --- Event Handlers ---
    void handle_user_input();
    void handle_network_message();
    
    // --- Readline Callback ---
    // Must be static to be used as a C-style function pointer.
    static void line_handler(char* line);
};