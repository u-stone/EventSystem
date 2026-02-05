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
    auto token1 = SubscribeMessage<std::string>("greeting", [](const std::string& msg) {
        std::cout << "    -> Received: " << msg << std::endl;
    });

    PublishMessage("greeting", std::string("Hello World")); 
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("greeting", token1);

    // 2. Zero Arguments
    std::cout << "\n[2] Zero Arguments" << std::endl;
    auto token2 = SubscribeMessage<>("ping", []() {
        std::cout << "    -> Ping received!" << std::endl;
    });

    PublishMessage("ping");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("ping", token2);

    // 3. Multiple Arguments
    std::cout << "\n[3] Multiple Arguments" << std::endl;
    auto token3 = SubscribeMessage<int, float>("coordinates", [](int x, float y) {
        std::cout << "    -> Coords: (" << x << ", " << y << ")" << std::endl;
    });

    PublishMessage("coordinates", 100, 55.5f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("coordinates", token3);

    // 4. Type Safety
    std::cout << "\n[4] Type Mismatch Safety" << std::endl;
    auto token4 = SubscribeMessage<int>("number", [](int n) {
        std::cout << "    -> Number: " << n << std::endl;
    });

    PublishMessage("number", std::string("Not a number")); 
    PublishMessage("number", 42);

    // 5. EventCenter Demo
    std::cout << "\n[5] EventCenter Demo" << std::endl;
    struct SampleEvent { int id; };
    auto eventHandle = SubscribeEvent<SampleEvent>([](const SampleEvent& e) {
        std::cout << "    -> Event Received: " << e.id << std::endl;
    });
    PublishEvent(SampleEvent{101});

    // 6. MessageCenter Dynamic Mode
    std::cout << "\n[6] MessageCenter Dynamic Mode" << std::endl;
    auto mainThreadId = std::this_thread::get_id();
    auto token5 = SubscribeMessage<>("mode_test", [mainThreadId]() {
        auto currentId = std::this_thread::get_id();
        std::cout << "    -> Mode: " 
                  << (currentId == mainThreadId ? "SYNC (Main Thread)" : "ASYNC (Worker Thread)") 
                  << std::endl;
    });

    std::cout << "  - Default (Async):" << std::endl;
    PublishMessage("mode_test");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "  - Switching to Sync:" << std::endl;
    SetMessageCenterPublishMode(PublishMode::Sync);
    PublishMessage("mode_test");

    std::cout << "  - Switching back to Async:" << std::endl;
    SetMessageCenterPublishMode(PublishMode::Async);
    PublishMessage("mode_test");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "  - Switching to MainThread (New Default):" << std::endl;
    SetMessageCenterPublishMode(PublishMode::MainThread);
    PublishMessage("mode_test");
    std::cout << "    (Message queued... calling UpdateMessageCenter())" << std::endl;
    UpdateMessageCenter();

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

    SetEventCenterPublishMode(PublishMode::Sync);
    PublishEvent(DynamicEvent{2});
    SetEventCenterPublishMode(PublishMode::Async);
    PublishEvent(DynamicEvent{3});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeEvent(dynamicHandle);

    // 8. Type Promotion
    std::cout << "\n[8] Type Promotion Demo" << std::endl;
    auto pToken = SubscribeMessage<std::string>("promo", [](const std::string& msg) {
        std::cout << "    -> Received string: " << msg << std::endl;
    });
    PublishMessage("promo", "literal");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("promo", pToken);

    // 9. Lambda Capture Demo
    std::cout << "\n[9] Lambda Capture Demo" << std::endl;
    class Player {
    public:
        std::string name;
        int score = 0;
        Player(std::string n) : name(n) {
            SubscribeMessage<int>("score", [this](int p) {
                this->score += p;
                std::cout << "    -> " << this->name << " total: " << this->score << std::endl;
            });
        }
    };
    {
        Player p1("Mario");
        PublishMessageSync("score", 100);
    }
    UnsubscribeMessage("score");

    // 10. Debug Info
    std::cout << "\n[10] Debug Info" << std::endl;
    PrintMessageSubscriptions();
    PrintEventSubscriptions();

    // Clean up
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UnsubscribeMessage("number", token4);
    UnsubscribeEvent(eventHandle);

    DestroyMessageCenter();
    DestroyEventCenter();

    std::cout << "\n--- Demo Finished ---" << std::endl;
    return 0;
}