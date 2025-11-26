#include "chat_app.hpp"

int main() {
    try {
        // Create an instance of the specific application
        ChatApp my_chat_server(PORT);
        
        // Call the run() method from the generic ServerEngine base class
        my_chat_server.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}