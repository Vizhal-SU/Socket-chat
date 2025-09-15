#include "network_utils.hpp"
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <set>
#include <poll.h>
#include "uuid.h"
#include <optional>

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

struct Room {
    std::string name;
    std::set<int> members; // Store the unique fds of clients in the room
    explicit Room(std::string name) : name(std::move(name)) {}
    bool hasMember(int fd) { return members.contains(fd); }
    void addMember(int fd) { members.insert(fd); }
    void removeMember(int fd) { members.erase(fd); }

};

struct ServerState {
    std::mutex mtx;
    std::map<int, ClientInfo> clients;           // fd -> ClientInfo
    std::map<std::string, Room> rooms;           // name -> Room
    std::map<int, std::string> client_to_room_name; // fd -> roomName
    std::set<int> pending_clients;
};

struct Command {
    std::string name;
    std::vector<std::string> args;
};

class ChatServer {
public:
    explicit ChatServer(const std::string& port);
    void run();

private:
    // Core I/O handlers
    void handle_new_connection();
    void handle_client_data(size_t client_index);
    void remove_client(size_t client_index);
    
    // Logic dispatchers
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

    // Member variables
    Socket listener_;
    std::vector<pollfd> fds_;
    ServerState state_;
};