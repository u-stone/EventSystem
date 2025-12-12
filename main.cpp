#include "EventSystem.h"
#include <string>
#include <chrono>
#include <iostream>
#include <thread>

// --- Define Event Types ---

// An event to be handled by the traditional class-based handler.
struct LegacyEvent
{
    std::string data;
};

// A simple event to be handled by a lightweight callback.
struct SimpleMessageEvent
{
    const char *message;
};

// --- 1. Traditional, Class-based Handler ---
class LegacyHandler : public IEventHandler
{
public:
    void handle(const std::any &eventData) override
    {
        if (auto *event = std::any_cast<LegacyEvent>(&eventData))
        {
            std::cout << "    -> [LegacyHandler] Received LegacyEvent: " << event->data << std::endl;
        }
    }
};

int main()
{
    std::cout << "--- Demo of Class-based vs. Callback-based Event Handling ---" << std::endl;

    // --- Part 1: Register a traditional IEventHandler class ---
    std::cout << "\n[1] Registering a class-based handler for LegacyEvent..." << std::endl;
    // Handlers must now be created as shared_ptr to be managed automatically.
    auto legacyHandler = std::make_shared<LegacyHandler>();
    EventCenter::instance().registerHandler<LegacyEvent>(legacyHandler);

    // --- Part 2: Register a lightweight callback (lambda) handler ---
    std::cout << "[2] Registering a lambda-based handler for SimpleMessageEvent..." << std::endl;
    auto simpleEventHandle = EventCenter::instance().registerHandler<SimpleMessageEvent>(
        // This lambda is the event handler. No new class needed.
        [](const SimpleMessageEvent &event)
        {
            std::cout << "    -> [Callback] Received SimpleMessageEvent: " << event.message << std::endl;
        });
    std::cout << "    (Obtained subscription handle: " << simpleEventHandle << ")" << std::endl;

    // --- Part 3: Publish both types of events ---
    std::cout << "\n[3] Publishing events..." << std::endl;
    publish_event(LegacyEvent{"Message for the class handler."});
    publish_event(SimpleMessageEvent{"Message for the lambda handler."});

    // Give time for the first batch of events to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // --- Part 4: Unregister the callback handler using its handle ---
    std::cout << "\n[4] Unregistering the lambda handler (handle: " << simpleEventHandle << ")..." << std::endl;
    EventCenter::instance().unregisterHandler(simpleEventHandle);

    // --- Part 5: Publish events again ---
    std::cout << "\n[5] Publishing events again..." << std::endl;
    publish_event(LegacyEvent{"This should be received by the class handler."});
    publish_event(SimpleMessageEvent{"!!! This message should NOT be seen !!!"});

    // --- Part 6: Unregister all handlers for a specific event ---

    std::cout << "\n[6] Demonstrating unregisterAllHandlers..." << std::endl;

    struct BroadcastEvent
    {
        const char *content;
    };

    // Register multiple handlers for the same event

    EventCenter::instance().registerHandler<BroadcastEvent>(

        [](const BroadcastEvent &e)
        { std::cout << "    -> [Broadcast CB 1] Got: " << e.content << std::endl; }

    );

    EventCenter::instance().registerHandler<BroadcastEvent>(

        [](const BroadcastEvent &e)
        { std::cout << "    -> [Broadcast CB 2] Got: " << e.content << std::endl; }

    );

    std::cout << "    Publishing BroadcastEvent, both callbacks should receive it." << std::endl;

    publish_event(BroadcastEvent{"First broadcast"});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Now, unregister all handlers for BroadcastEvent

    std::cout << "    Calling unregisterAllHandlers<BroadcastEvent>()..." << std::endl;

    EventCenter::instance().unregisterAllHandlers<BroadcastEvent>();

    std::cout << "    Publishing again, no handlers should receive it." << std::endl;

    publish_event(BroadcastEvent{"!!! THIS SHOULD NOT BE SEEN !!!"});

    // --- Finalization ---

    std::cout << "\nWaiting for final events to be processed..." << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Demo finished." << std::endl;

    // The EventCenter singleton will be destroyed here as the program exits.

    return 0;
}
