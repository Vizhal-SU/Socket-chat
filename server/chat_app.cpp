#include "chat_app.hpp"
#include <iostream>
#include <sstream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stop_token>
#include "network_utils.hpp"

ChatApp::ChatApp(const std::string& port) : ServerEngine<ChatApp>(port) {
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    std::cout << "Spawning " << num_threads << " worker jthreads...\n";
    for (unsigned int i = 0; i < num_threads; ++i) {
        // Use a lambda to correctly capture 'this' and pass the stop_token
        worker_threads_.emplace_back([this](std::stop_token token) {
            this->worker_loop(token);
        });
    }
}

ChatApp::~ChatApp() {
    // 1. Request all threads to stop.
    for (auto& t : worker_threads_) {
        t.request_stop();
    }
    // 2. The destructors of the std::jthread objects in the vector
    //    will automatically call join(). No manual join loop needed.
    std::cout << "Shutting down worker threads...\n";
}

// --- PRODUCER LOGIC (called by the main I/O thread) ---

void ChatApp::on_connect(int fd) {
    std::lock_guard<std::mutex> lk(state_.mtx);
    state_.pending_clients.insert(fd);
    std::cout << "New pending connection on fd " << fd << std::endl;
}

void ChatApp::on_disconnect(int fd) {
    task_queue_.push({TaskType::Disconnection, fd, ""});
}

void ChatApp::on_message(int fd, const std::string& message) {
    task_queue_.push({TaskType::NewMessage, fd, message});
}

// --- CONSUMER LOGIC (executed by the worker threads) ---

// The worker_loop handles the handshake state.
void ChatApp::worker_loop(const std::stop_token token) {
    while (!token.stop_requested()) {
        Task task;
        if (task_queue_.pop(task, token)) {
            switch (task.type) {
                case TaskType::NewMessage:
                {
                    bool is_pending;
                    {
                        std::lock_guard<std::mutex> lk(state_.mtx);
                        is_pending = state_.pending_clients.count(task.client_fd);
                    }

                    // If the client is pending, this message is their name.
                    if (is_pending) {
                        handle_handshake(task.client_fd, task.data);
                    } else { // Otherwise, it's a normal message or command.
                        std::string line = task.data;
                        if (!line.empty() && line.back() == '\n') line.pop_back();

                        if (auto command = parse_command(line)) {
                            handle_command(task.client_fd, *command);
                        } else {
                            handle_chat_message(task.client_fd, line);
                        }
                    }
                    break;
                }
                case TaskType::Disconnection:
                {
                    std::string name;
                    handle_leave_command(task.client_fd);
                    {
                        std::lock_guard<std::mutex> lk(state_.mtx);
                        if (state_.clients.count(task.client_fd)) {
                            name = state_.clients.at(task.client_fd).name;
                            state_.clients.erase(task.client_fd);
                        }
                        state_.pending_clients.erase(task.client_fd); // Also clean up pending
                    }
                    if (!name.empty()) {
                        std::cout << name << " disconnected.\n";
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void ChatApp::handle_handshake(int client_fd, const std::string& name_data) {
    std::string name = name_data;
    if (name.empty() || name.length() > 32) {
        this->send_to_client(client_fd, "[Error]: Invalid name. Connection will be closed.\n");
        ::close(client_fd);
        std::lock_guard<std::mutex> lk(state_.mtx);
        state_.pending_clients.erase(client_fd);
        return;
    }
    if (name.back() == '\n') name.pop_back();
    std::string color = COLORS[client_fd % COLORS.size()];
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        state_.pending_clients.erase(client_fd);
        state_.clients[client_fd] = {name, color};
    }

    std::cout << name << " connected on fd " << client_fd << ".\n";
    std::string welcome_msg = "[System]: Welcome, " + name + "! Create a room with $create <name> or join with $join <name>\n";
    this->send_to_client(client_fd, welcome_msg); // Use the engine to send
}

std::optional<Command> ChatApp::parse_command(const std::string& line) {
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

void ChatApp::handle_command(int client_fd, const Command& command) {
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
    } else {
        this->send_to_client(client_fd, "[Error]: Unknown command '" + command.name + "'.\n");
    }
}

void ChatApp::handle_chat_message(int client_fd, const std::string& msg) {
    std::string room_name;
    std::string formatted_msg;
    bool is_in_room = false;

    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        if (state_.client_to_room_name.count(client_fd)) {
            is_in_room = true;
            room_name = state_.client_to_room_name.at(client_fd);
            const auto& info = state_.clients.at(client_fd);
            formatted_msg = info.color + "[" + info.name + "]: " + RESET + msg + "\n";
        }
    }

    if (is_in_room) {
        broadcast_to_room(room_name, formatted_msg, client_fd);
        std::cout << formatted_msg;
    } else {
        this->send_to_client(client_fd, "[Error]: You must join a room to chat. Use $join <name>\n");
    }
}

bool ChatApp::handle_create_command(int client_fd, const std::vector<std::string>& args) {
    if (args.empty()) {
        this->send_to_client(client_fd, "[Error]: Usage: $create <room_name>\n");
        return false;
    }
    const std::string& room_name = args[0];
    
    std::lock_guard<std::mutex> lk(state_.mtx);
    if (state_.rooms.count(room_name)) {
        this->send_to_client(client_fd, "[Error]: Room '" + room_name + "' already exists.\n");
        return false;
    } 
    state_.rooms.emplace(room_name, Room(room_name));
    std::cout << "Room '" << room_name << "' created.\n";
    this->send_to_client(client_fd, "[System]: Room '" + room_name + "' created.\n");
    return true;
}

void ChatApp::handle_join_command(int client_fd, const std::vector<std::string>& args) {
    if (args.empty()) {
        this->send_to_client(client_fd, "[Error]: Usage: $join <room_name>\n");
        return;
    }
    const std::string& room_name = args[0];
    std::string user_name;
    std::string old_room_name;
    bool was_in_room = false;

    {
        std::unique_lock<std::mutex> lk(state_.mtx);
        if (!state_.rooms.count(room_name)) {
            this->send_to_client(client_fd, "[Error]: Room '" + room_name + "' does not exist.\n");
            return;
        }
        user_name = state_.clients.at(client_fd).name;
        
        if (state_.client_to_room_name.count(client_fd)) {
            old_room_name = state_.client_to_room_name.at(client_fd);
            if (old_room_name == room_name) {
                this->send_to_client(client_fd, "[Error]: You are already in that room.\n");
                return;
            }
            was_in_room = true;
            state_.rooms.at(old_room_name).removeMember(client_fd);
            state_.client_to_room_name.erase(client_fd);
        }
        state_.rooms.at(room_name).addMember(client_fd);
        state_.client_to_room_name[client_fd] = room_name;

    } // Lock is released here. Now we can do broadcasts without holding the main lock.

    if (was_in_room) {
        std::string leave_msg = "\n[System]: " + user_name + " has left room " + old_room_name + ".\n";
        broadcast_to_room(old_room_name, leave_msg, -1);
    }

    std::string join_msg = "\n[System]: " + user_name + " has joined the room " + room_name + ".\n";
    broadcast_to_room(room_name, join_msg, client_fd);
}

void ChatApp::handle_leave_command(int client_fd) {
    std::string room_name;
    std::string user_name;
    bool was_in_room = false;

    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        if (state_.client_to_room_name.count(client_fd)) {
            was_in_room = true;
            room_name = state_.client_to_room_name.at(client_fd);
            user_name = state_.clients.at(client_fd).name;
            state_.rooms.at(room_name).removeMember(client_fd);
            state_.client_to_room_name.erase(client_fd);
        }
    }

    if (was_in_room) {
        std::string leave_msg = "\n[System]: " + user_name + " has left the room.\n";
        broadcast_to_room(room_name, leave_msg, -1);
        this->send_to_client(client_fd, "[System]: You have left room '" + room_name + "'.\n");
    } else {
        this->send_to_client(client_fd, "[Error]: You are not in a room.\n");
    }
}

void ChatApp::handle_list_rooms_command(int client_fd) {
    std::string room_list = "[System]: Available rooms:\n";
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        if (state_.rooms.empty()) {
            room_list += "  (No rooms available. Create one with $create <name>)\n";
        } else {
            for (const auto& [name, room] : state_.rooms) {
                room_list += "  - " + name + " (" + std::to_string(room.members.size()) + " members)\n";
            }
        }
    }
    this->send_to_client(client_fd, room_list);
}

void ChatApp::handle_list_members_command(int client_fd) {
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
    this->send_to_client(client_fd, member_list);
}

void ChatApp::broadcast_to_room(const std::string& room_name, std::string_view msg, int sender_fd_to_skip) {
    std::lock_guard<std::mutex> lock(state_.mtx);
    if (state_.rooms.count(room_name)) {
        const auto& room = state_.rooms.at(room_name);
        for (int member_fd : room.members) {
            this->send_to_client(member_fd, msg);
        }
    }
}