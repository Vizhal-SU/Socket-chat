cmake_minimum_required(VERSION 3.14)
project(SocketChat LANGUAGES CXX)

# Require C++17 (or newer if you like)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(client client.cpp)
target_link_libraries(client PRIVATE readline ncurses pthread)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE pthread)
