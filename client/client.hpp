#pragma once
#include "network_utils.hpp"
#include <string>
#include <atomic>

class ChatClient {
public:
    static ChatClient& instance(const std::string& host, const std::string& port);

    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    void run();

private:
    ChatClient(const std::string& host, const std::string& port);
    
    // --- Member Variables (State) ---
    Socket sock_;
    std::string name_;
    std::atomic<bool> running_{true};

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