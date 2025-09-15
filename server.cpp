#include "server.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sstream>

ChatServer::ChatServer(const std::string& port) : listener_(get_listener_socket(port.c_str())) {
    if (!listener_) throw std::runtime_error("Failed to initialize listener socket.");
    fds_.push_back({listener_.get(), POLLIN, 0});
}

void ChatServer::run() {
    std::cout << "Server listening on port " << PORT << "...\n";
    while (true) {
        if (::poll(fds_.data(), fds_.size(), -1) < 0) {
            perror("poll");
            break;
        }
        for (size_t i = 0; i < fds_.size(); ++i) {
            if (fds_[i].revents & POLLIN) {
                if (fds_[i].fd == listener_.get()) {
                    handle_new_connection();
                } else {
                    handle_client_data(i);
                }
            }
        }
    }
}

void ChatServer::handle_new_connection() {
    int client_fd = ::accept(listener_.get(), nullptr, nullptr);
    if (client_fd < 0) {
        perror("accept");
        return;
    }

    // Make the new socket non-blocking
    if (!set_non_blocking(client_fd)) {
        ::close(client_fd);
        return;
    }

    fds_.push_back({client_fd, POLLIN, 0});

    // Add client to pending set to await name handshake
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        state_.pending_clients.insert(client_fd);
    }
    std::cout << "New pending connection on fd " << client_fd << std::endl;
}

void ChatServer::remove_client(size_t client_index) {
    int client_fd = fds_[client_index].fd;
    std::string name;
    
    handle_leave_command(client_fd);

    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        if (state_.clients.count(client_fd)) {
            name = state_.clients.at(client_fd).name;
            state_.clients.erase(client_fd);
        }
    }

    if (!name.empty()) {
        std::cout << name << " disconnected.\n";
    }

    ::close(client_fd);
    fds_.erase(fds_.begin() + client_index);
}

void ChatServer::handle_client_data(size_t client_index) {
    int client_fd = fds_[client_index].fd;
    bool is_pending;
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        is_pending = state_.pending_clients.count(client_fd);
    }

    if (is_pending) {
        // --- HANDSHAKE LOGIC ---
        char buf[128];
        ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (n <= 0) { // Disconnected during handshake
            remove_client(client_index);
            return;
        }

        buf[n] = '\0';
        std::string name(buf);
        if (!name.empty() && name.back() == '\n') name.pop_back();

        std::string color = COLORS[client_fd % COLORS.size()];
        std::string join_msg = "\n[System]: " + name + " has joined the chat.\n";
        
        {
            std::lock_guard<std::mutex> lk(state_.mtx);
            // Handshake successful: move from pending to active clients
            state_.pending_clients.erase(client_fd);
            state_.clients[client_fd] = {name, color};
        }

        std::cout << name << " connected on fd " << client_fd << ".\n";
        send_all(client_fd, "[System]: Welcome! Join a room with $join <room_name>\n");

    } else {
        // --- REGULAR MESSAGE LOGIC ---
        char buf[MAXDATASIZE];
        ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            remove_client(client_index);
        } else {
            buf[n] = '\0';
            std::string line(buf);
            if (!line.empty() && line.back() == '\n') line.pop_back();

            if (auto command = parse_command(line)) {
                handle_command(client_fd, *command);
            } else {
                handle_chat_message(client_fd, line);
            }
        }
    }
}

std::optional<Command> ChatServer::parse_command(const std::string& line) {
    if (line.empty() || line.rfind("$", 0) != 0) return std::nullopt;
    std::stringstream ss(line);
    std::string command_str;
    ss >> command_str;
    Command cmd;
    cmd.name = command_str.substr(1);
    std::string arg;
    while (ss >> arg) cmd.args.push_back(arg);
    return cmd;
}

void ChatServer::handle_command(int client_fd, const Command& command) {
    if (command.name == "create") {
        if (handle_create_command(client_fd, command.args)) {
            handle_join_command(client_fd, command.args);
        }
    } else if (command.name == "join") {
        handle_join_command(client_fd, command.args);
    } else if (command.name == "leave") {
        handle_leave_command(client_fd);
    } else if (command.name == "list_rooms") {
        handle_list_rooms_command(client_fd);
    } else if (command.name == "list_members") {
        handle_list_members_command(client_fd);
    }
    else {
        send_all(client_fd, "[Error]: Unknown command '" + command.name + "'.\n");
    }
}

void ChatServer::handle_chat_message(int client_fd, const std::string& msg) {
    // Use std::unique_lock to allow for manual unlocking
    std::unique_lock<std::mutex> lk(state_.mtx);

    if (state_.client_to_room_name.count(client_fd)) {
        std::string room_name = state_.client_to_room_name.at(client_fd);
        const auto& info = state_.clients.at(client_fd);
        std::string formatted_msg = info.color + "[" + info.name + "]: " + RESET + msg + "\n";
        lk.unlock(); 
        
        broadcast_to_room(room_name, formatted_msg, client_fd);
        std::cout << formatted_msg;
    } else {
        lk.unlock();
        send_all(client_fd, "[Error]: You must join a room to chat. Use $join <room_name>\n");
    }
}

// --- Specific Command Handlers ---

bool ChatServer::handle_create_command(int client_fd, const std::vector<std::string>& args) {
    if (args.empty()) {
        send_all(client_fd, "[Error]: Usage: $create <room_name>\n");
        return false;
    }
    const std::string& room_name = args[0];
    std::lock_guard<std::mutex> lk(state_.mtx);
    if (state_.rooms.count(room_name)) {
        send_all(client_fd, "[Error]: Room '" + room_name + "' already exists.\n");
        return false;
    } else {
        state_.rooms.emplace(room_name, Room(room_name));
        send_all(client_fd, "[System]: Room '" + room_name + "' created.\n");
        return true;
    }
}

void ChatServer::handle_join_command(int client_fd, const std::vector<std::string>& args) {
    if (args.empty()) {
        send_all(client_fd, "[Error]: Usage: $join <room_name>\n");
        return;
    }
    const std::string& room_name = args[0];
    std::string user_name;

    std::unique_lock<std::mutex> lk(state_.mtx);
    if (!state_.rooms.count(room_name)) {
        send_all(client_fd, "[Error]: Room '" + room_name + "' does not exist.\n");
        return;
    }
    user_name = state_.clients.at(client_fd).name;
    
    // Leave current room if in one
    if (state_.client_to_room_name.count(client_fd)) {
        std::string old_room_name = state_.client_to_room_name.at(client_fd);
        if (old_room_name == room_name) {
            send_all(client_fd, "[Error]: You are already in that room.\n");
            return;
        }
        state_.rooms.at(old_room_name).removeMember(client_fd);
        std::string leave_msg = "\n[System]: " + user_name + " has left the room.\n";
        lk.unlock();
        broadcast_to_room(old_room_name, leave_msg, -1);
        lk.lock();
    }

    // Add user to new room
    state_.rooms.at(room_name).addMember(client_fd);
    state_.client_to_room_name[client_fd] = room_name;
    std::string join_msg = "\n[System]: " + user_name + " has joined the room.\n";
    
    lk.unlock();
    broadcast_to_room(room_name, join_msg, client_fd);
    send_all(client_fd, "[System]: You have joined room '" + room_name + "'.\n");
}

void ChatServer::handle_leave_command(int client_fd) {
    std::unique_lock<std::mutex> lk(state_.mtx);
    if (state_.client_to_room_name.count(client_fd)) {
        std::string room_name = state_.client_to_room_name.at(client_fd);
        std::string user_name = state_.clients.at(client_fd).name;

        state_.rooms.at(room_name).removeMember(client_fd);
        state_.client_to_room_name.erase(client_fd);
        
        std::string leave_msg = "\n[System]: " + user_name + " has left the room.\n";
        lk.unlock();
        broadcast_to_room(room_name, leave_msg, -1);
        send_all(client_fd, "[System]: You have left room '" + room_name + "'.\n");
    } else {
        send_all(client_fd, "[Error]: You are not in a room.\n");
    }
}

void ChatServer::handle_list_rooms_command(int client_fd) {
    std::string room_list = "[System]: Available rooms:\n";
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        if (state_.rooms.empty()) {
            room_list += "  (No rooms available)\n";
        } else {
            for (const auto& [name, room] : state_.rooms) {
                room_list += "  - " + name + " (" + std::to_string(room.members.size()) + " members)\n";
            }
        }
    }
    send_all(client_fd, room_list);
}

void ChatServer::handle_list_members_command(int client_fd) {
    std::string member_list;
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        if (state_.client_to_room_name.count(client_fd)) {
            std::string room_name = state_.client_to_room_name.at(client_fd);
            member_list = "[System]: Members in '" + room_name + "':\n";
            const auto& room = state_.rooms.at(room_name);
            if (room.members.empty()) {
                member_list += "  (This room is empty)\n";
            } else {
                for (int member_fd : room.members) {
                    member_list += "  - " + state_.clients.at(member_fd).name + "\n";
                }
            }
        } else {
            member_list = "[Error]: You are not in a room.\n";
        }
    }
    send_all(client_fd, member_list);
}

void ChatServer::broadcast_to_room(const std::string& room_name, std::string_view msg, int sender_fd_to_skip) {
    std::lock_guard<std::mutex> lock(state_.mtx);
    if (state_.rooms.count(room_name)) {
        const auto& room = state_.rooms.at(room_name);
        for (int member_fd : room.members) {
            send_all(member_fd, msg);
        }
    }
}

int main() {
    try {
        ChatServer server(PORT);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}