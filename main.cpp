#include "EventSystem.h"
#include <string>
#include <chrono>
#include <iostream>
#include <thread>

// --- Event & Handler for Weak Registration Demo ---
struct ManagedEvent { std::string data; };
class ManagedHandler : public IEventHandler {
public:
    void handle(const std::any& eventData) override {
        if (auto* event = std::any_cast<ManagedEvent>(&eventData)) {
            std::cout << "    -> [ManagedHandler] Received: " << event->data << std::endl;
        }
    }
    ~ManagedHandler() { std::cout << "    -> [ManagedHandler] Destructor called. Lifetime managed externally." << std::endl; }
};

// --- Event & Handler for Strong Registration Demo ---
struct FireAndForgetEvent { };
class FireAndForgetHandler : public IEventHandler {
public:
    void handle(const std::any& eventData) override {
        if (std::any_cast<FireAndForgetEvent>(&eventData)) {
            std::cout << "    -> [FireAndForgetHandler] Received event. I am kept alive by EventCenter." << std::endl;
        }
    }
    ~FireAndForgetHandler() { std::cout << "    -> [FireAndForgetHandler] Destructor called. Was released by EventCenter." << std::endl; }
};

// --- Event for Callback Demo ---
struct SimpleMessageEvent { const char* message; };


int main() {
    std::cout << "--- Advanced Event Handling Demo ---" << std::endl;

    // ===================================================================================
    // STEP 1: Weak Handler Registration (Lifetime managed by caller)
    // ===================================================================================
    std::cout << "\n[1] DEMO: registerWeakHandler - for externally managed lifetimes." << std::endl;
    auto managedHandler = std::make_shared<ManagedHandler>();
    EventCenter::instance().registerWeakHandler<ManagedEvent>(managedHandler);

    std::cout << "  - Publishing ManagedEvent. Handler should receive it." << std::endl;
    publish_event(ManagedEvent{"Initial message"});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "  - Releasing external shared_ptr. Handler object will be destroyed." << std::endl;
    managedHandler.reset(); // Destroy the handler object

    std::cout << "  - Publishing again. Handler should NOT receive it (weak_ptr has expired)." << std::endl;
    publish_event(ManagedEvent{"!!! THIS SHOULD NOT BE SEEN !!!"});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // ===================================================================================
    // STEP 2: Strong Handler Registration (Lifetime managed by EventCenter)
    // ===================================================================================
    std::cout << "\n[2] DEMO: registerHandler - for 'fire-and-forget' convenience." << std::endl;
    std::cout << "  - Registering handler via temporary r-value shared_ptr." << std::endl;
    EventCenter::instance().registerHandler<FireAndForgetEvent>(
        std::make_shared<FireAndForgetHandler>()
    );

    std::cout << "  - Publishing FireAndForgetEvent. Handler is alive and should receive it." << std::endl;
    publish_event(FireAndForgetEvent{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "  - Handler object still alive. Must unregister to release it." << std::endl;
    EventCenter::instance().unregisterAllHandlers<FireAndForgetEvent>();


    // ===================================================================================
    // STEP 3: Callback Handler Registration
    // ===================================================================================
    std::cout << "\n[3] DEMO: Callback (lambda) registration." << std::endl;
    auto cb_handle = EventCenter::instance().registerHandler<SimpleMessageEvent>(
        [](const SimpleMessageEvent& event) {
            std::cout << "    -> [Callback] Received: " << event.message << std::endl;
        }
    );

    std::cout << "  - Publishing SimpleMessageEvent. Callback should receive it." << std::endl;
    publish_event(SimpleMessageEvent{"Message for lambda"});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "  - Unregistering callback via its handle." << std::endl;
    EventCenter::instance().unregisterHandler(cb_handle);
    publish_event(SimpleMessageEvent{"!!! THIS SHOULD NOT BE SEEN !!!"});
    

    // --- Finalization ---
    std::cout << "\n--- Demo Finished ---" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
