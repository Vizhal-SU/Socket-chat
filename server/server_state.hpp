#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <set>
#include <optional>

const std::vector<std::string> COLORS = {"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[36m"};
const std::string RESET = "\033[0m";

struct ClientInfo {
    std::string name;
    std::string color;
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

// Represents a piece of work for a worker thread
enum class TaskType {
    NewMessage,
    Disconnection
};

struct Task {
    TaskType type;
    int client_fd;
    std::string data; // For message content or handshake name
};