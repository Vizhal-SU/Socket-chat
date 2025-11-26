cmake_minimum_required(VERSION 3.14)
project(SocketChat LANGUAGES CXX)

# Set the C++ standard to C++23, as you've been using
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Find the threads library (pthreads) in a modern, portable way
find_package(Threads REQUIRED)

# Add the project's root directory to the include path.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# --- Server Target ---
# Add an executable for the server, listing all its source files.
# Make sure your main server file is named server_main.cpp or adjust as needed.
add_executable(server
    server/chat_app.cpp
    server/main.cpp 
)

# Link the server against the necessary libraries: pthreads.
target_link_libraries(server PRIVATE
    Threads::Threads
)

# --- Client Target ---
# Add an executable for the client.
add_executable(client
    client/client.cpp
)

# Link the client against the necessary libraries: pthreads and readline.
target_link_libraries(client PRIVATE
    Threads::Threads
    readline
)