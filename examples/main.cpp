#include "EventSystem/EventCenter.h"
#include "EventSystem/MessageCenter.h"
#include <string>
#include <iostream>
#include <thread>
#include <chrono>

using namespace eventsystem;

int main() {
    std::cout << "--- Robust MessageCenter Demo ---" << std::endl;

    // 1. Classic String Message
    std::cout << "\n[1] String Message (Legacy Style)" << std::endl;
    // Explicitly specifying <std::string> is cleanest for lambdas with the new variadic API
    auto token1 = subscribe_message<std::string>("greeting", [](const std::string& msg) {
        std::cout << "    -> Received: " << msg << std::endl;
    });

    publish_message("greeting", std::string("Hello World")); // Async
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    unsubscribe_message("greeting", token1);

    // 2. Zero Arguments (Signal)
    std::cout << "\n[2] Zero Arguments (Void Signal)" << std::endl;
    auto token2 = subscribe_message<>("ping", []() {
        std::cout << "    -> Ping received!" << std::endl;
    });

    publish_message("ping"); // Triggers void()
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    unsubscribe_message("ping", token2);

    // 3. Multiple Arguments (int, float)
    std::cout << "\n[3] Multiple Arguments" << std::endl;
    auto token3 = subscribe_message<int, float>("coordinates", [](int x, float y) {
        std::cout << "    -> Coords: (" << x << ", " << y << ")" << std::endl;
    });

    publish_message("coordinates", 100, 55.5f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    unsubscribe_message("coordinates", token3);

    // 4. Type Safety / Mismatch
    std::cout << "\n[4] Type Mismatch Safety" << std::endl;
    auto token4 = subscribe_message<int>("number", [](int n) {
        std::cout << "    -> Number: " << n << std::endl;
    });

    std::cout << "  - Publishing string to 'number' (Should be ignored safely)..." << std::endl;
    publish_message("number", std::string("Not a number")); 
    
    std::cout << "  - Publishing int to 'number'..." << std::endl;
    publish_message("number", 42);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    unsubscribe_message("number", token4);

    std::cout << "\n--- Demo Finished ---" << std::endl;
    return 0;
}
