#pragma once
#include "server_engine.hpp" // The generic engine
#include "server_state.hpp"  // The application's data structures
#include "thread_safe_queue.hpp"
#include <string>
#include <thread>

// This is the specific Chat Application logic.
// It inherits from the generic ServerEngine, passing itself as the template
// parameter. This is the "Curiously Recurring Template Pattern".
class ChatApp : public ServerEngine<ChatApp> {
    // Allows the base class ServerEngine to access this class's private members/constructor
    friend class ServerEngine<ChatApp>;

public:
    // The public constructor calls the base class (ServerEngine) constructor.
    explicit ChatApp(const std::string& port);
    ~ChatApp();

    // These are the specific implementations required by the ServerEngine.
    // They are the main entry points from the networking layer into the application logic.
    void on_connect(int fd);
    void on_disconnect(int fd);
    void on_message(int fd, const std::string& message);

private:
    // All application-specific state is held here
    ServerState state_;
    ThreadSafeQueue<Task> task_queue_;
    std::vector<std::jthread> worker_threads_;

    // --- Private Helper Methods ---

    void worker_loop(std::stop_token token);

    // Logic dispatchers
    void handle_handshake(int client_fd, const std::string& name_data);
    std::optional<Command> parse_command(const std::string& line);
    void handle_command(int client_fd, const Command& command);
    void handle_chat_message(int client_fd, const std::string& msg);

    // Specific command handlers
    bool handle_create_command(int client_fd, const std::vector<std::string>& args);
    void handle_join_command(int client_fd, const std::vector<std::string>& args);
    void handle_leave_command(int client_fd);
    void handle_list_rooms_command(int client_fd);
    void handle_list_members_command(int client_fd);

    // Messaging
    void broadcast_to_room(const std::string& room_name, std::string_view msg, int sender_fd_to_skip);
};