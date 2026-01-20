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
    auto token1 = SubscribeMessage<std::string>("greeting", [](const std::string& msg) {
        std::cout << "    -> Received: " << msg << std::endl;
    });

    PublishMessage("greeting", std::string("Hello World")); // Async
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("greeting", token1);

    // 2. Zero Arguments (Signal)
    std::cout << "\n[2] Zero Arguments (Void Signal)" << std::endl;
    auto token2 = SubscribeMessage<>("ping", []() {
        std::cout << "    -> Ping received!" << std::endl;
    });

    PublishMessage("ping"); // Triggers void()
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("ping", token2);

    // 3. Multiple Arguments (int, float)
    std::cout << "\n[3] Multiple Arguments" << std::endl;
    auto token3 = SubscribeMessage<int, float>("coordinates", [](int x, float y) {
        std::cout << "    -> Coords: (" << x << ", " << y << ")" << std::endl;
    });

    PublishMessage("coordinates", 100, 55.5f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("coordinates", token3);

    // 4. Type Safety / Mismatch
    std::cout << "\n[4] Type Mismatch Safety" << std::endl;
    auto token4 = SubscribeMessage<int>("number", [](int n) {
        std::cout << "    -> Number: " << n << std::endl;
    });

    std::cout << "  - Publishing string to 'number' (Should be ignored safely)..." << std::endl;
    PublishMessage("number", std::string("Not a number")); 
    
    std::cout << "  - Publishing int to 'number'..." << std::endl;
    PublishMessage("number", 42);

    // 5. EventCenter Demo
    struct SampleEvent { int id; };
    auto eventHandle = SubscribeEvent<SampleEvent>([](const SampleEvent& e) {
        std::cout << "    -> Event Received: " << e.id << std::endl;
    });
    PublishEvent(SampleEvent{101});

    // 6. Debug Info
    std::cout << "\n[6] Debug Info" << std::endl;
    MessageCenter::Instance().PrintSubscriptions();
    EventCenter::PrintSubscriptions();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("number", token4);
    UnsubscribeEvent(eventHandle);

    std::cout << "\n--- Demo Finished ---" << std::endl;
    return 0;
}