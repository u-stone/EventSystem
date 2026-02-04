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
    std::cout << "\n[5] EventCenter Demo" << std::endl;
    struct SampleEvent { int id; };
    auto eventHandle = SubscribeEvent<SampleEvent>([](const SampleEvent& e) {
        std::cout << "    -> Event Received: " << e.id << std::endl;
    });
    PublishEvent(SampleEvent{101});

    // 6. Dynamic Mode Switching (MessageCenter)
    std::cout << "\n[6] MessageCenter Dynamic Mode" << std::endl;
    auto mainThreadId = std::this_thread::get_id();
    auto token5 = SubscribeMessage<>("mode_test", [mainThreadId]() {
        auto currentId = std::this_thread::get_id();
        if (currentId == mainThreadId) {
            std::cout << "    -> Mode: SYNC (Main Thread)" << std::endl;
        } else {
            std::cout << "    -> Mode: ASYNC (Worker Thread)" << std::endl;
        }
    });

    std::cout << "  - Default (Async):" << std::endl;
    PublishMessage("mode_test");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "  - Switching to Sync:" << std::endl;
    MessageCenter::Instance().SetPublishMode(PublishMode::Sync);
    PublishMessage("mode_test");

    std::cout << "  - Switching back to Async:" << std::endl;
    MessageCenter::Instance().SetPublishMode(PublishMode::Async);
    PublishMessage("mode_test");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    UnsubscribeMessage("mode_test", token5);

    // 7. EventCenter Dynamic Mode
    std::cout << "\n[7] EventCenter Dynamic Mode" << std::endl;
    struct DynamicEvent { int id; };
    auto dynamicHandle = SubscribeEvent<DynamicEvent>([mainThreadId](const DynamicEvent& e) {
        auto currentId = std::this_thread::get_id();
        std::cout << "    -> Event " << e.id << " on " 
                  << (currentId == mainThreadId ? "Main Thread (SYNC)" : "Worker Thread (ASYNC)") 
                  << std::endl;
    });

    std::cout << "  - Default (Async):" << std::endl;
    PublishEvent(DynamicEvent{1});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cout << "  - Switch to Sync:" << std::endl;
    EventRegistry::SetPublishMode(PublishMode::Sync);
    PublishEvent(DynamicEvent{2});

    std::cout << "  - Switch back to Async:" << std::endl;
    EventRegistry::SetPublishMode(PublishMode::Async);
    PublishEvent(DynamicEvent{3});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    UnsubscribeEvent(dynamicHandle);

    // 8. Debug Info
    std::cout << "\n[8] Debug Info" << std::endl;
    MessageCenter::Instance().PrintSubscriptions();
    EventCenter::PrintSubscriptions();

    // 9. Type Promotion Demo (const char* -> std::string)
    std::cout << "\n[9] Type Promotion Demo" << std::endl;
    auto promotionToken = SubscribeMessage<std::string>("promotion_test", [](const std::string& msg) {
        std::cout << "    -> Received (std::string): " << msg << std::endl;
    });
    
    // Publishing const char*, automatically promoted to std::string
    PublishMessage("promotion_test", "This is a string literal"); 
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("promotion_test", promotionToken);

    // 10. Lambda Capture Demo (capturing this)
    std::cout << "\n[10] Lambda Capture Demo" << std::endl;
    class Player {
    public:
        std::string name;
        int score = 0;
        size_t token = 0;

        Player(std::string n) : name(n) {
            // Capture 'this' to access member variables (int arg)
            token = SubscribeMessage<int>("add_score", [this](int points) {
                this->score += points;
                std::cout << "    -> " << this->name << " gained " << points << " points. Total: " << this->score << std::endl;
            });

            // Capture 'this' with std::string arg (demonstrating promotion too)
            SubscribeMessage<std::string>("player_msg", [this](const std::string& msg) {
                std::cout << "    -> " << this->name << " says: " << msg << std::endl;
            });
        }

        ~Player() {
            UnsubscribeMessage("add_score", token);
            UnsubscribeMessage("player_msg");
            // Note: In a real app, you should store and unsubscribe the second token too.
            // For this short-lived demo scope, it's cleaned up by the system on shutdown or we can unsubscribe all for topic.
        }
    };

    {
        Player p1("Mario");
        PublishMessageSync("add_score", 100);
        PublishMessageSync("add_score", 50);
        PublishMessageSync("player_msg", "It's a me!"); // String literal promotion
        // p1 goes out of scope here
    }
    std::cout << "    -> Player destroyed, subscription removed." << std::endl;

    // Verify unsubscription
    PublishMessageSync("add_score", 10); // Should print nothing

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("number", token4);
    UnsubscribeEvent(eventHandle);

    std::cout << "\n--- Demo Finished ---" << std::endl;

    // Explicitly destroy singletons to stop worker threads cleanly
    MessageCenter::Destroy();
    AsyncEventCenter::Destroy();

    return 0;
}