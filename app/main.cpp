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


    // ===================================================================================
    // STEP 4: Static Handler Registration (Self-contained event & handler)
    // ===================================================================================
    std::cout << "\n[4] DEMO: Static handler registration for stateless logic." << std::endl;
    // This struct contains its own handler as a static method.
    struct SelfHandledEvent {
        const char* text;
        static void handle(const SelfHandledEvent& event) {
            std::cout << "    -> [Static Handler] Received SelfHandledEvent with text: '" << event.text << "'" << std::endl;
        }
    };

    std::cout << "  - Registering event's static handle method with one call." << std::endl;
    auto static_handle = registerStaticEventHandler<SelfHandledEvent>();

    std::cout << "  - Publishing SelfHandledEvent." << std::endl;
    publish_event(SelfHandledEvent{"This is very convenient!"});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "  - Unregistering static handler via its handle." << std::endl;
    EventCenter::instance().unregisterHandler(static_handle);
    publish_event(SelfHandledEvent{"!!! THIS SHOULD NOT BE SEEN !!!"});


    // ===================================================================================
    // STEP 5: Delayed Event Publishing
    // ===================================================================================
    std::cout << "\n[5] DEMO: publish_event_delayed - Processing after a delay." << std::endl;
    auto delayed_handle = EventCenter::instance().registerHandler<SimpleMessageEvent>(
        [](const SimpleMessageEvent& event) {
             std::cout << "    -> [Delayed] Received: " << event.message << std::endl;
        }
    );

    std::cout << "  - Publishing event with 200ms delay..." << std::endl;
    publish_event_delayed(SimpleMessageEvent{"I am late!"}, std::chrono::milliseconds(200));
    
    // We need to wait enough time to see it happen
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EventCenter::instance().unregisterHandler(delayed_handle);


    // ===================================================================================
    // STEP 6: Scheduled Event Publishing (at specific time)
    // ===================================================================================
    std::cout << "\n[6] DEMO: publish_event_at - Processing at specific time point." << std::endl;
    auto scheduled_handle = EventCenter::instance().registerHandler<SimpleMessageEvent>(
        [](const SimpleMessageEvent& event) {
             std::cout << "    -> [Scheduled] Received: " << event.message << std::endl;
        }
    );

    auto future_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    std::cout << "  - Publishing event scheduled for 300ms in the future..." << std::endl;
    publish_event_at(SimpleMessageEvent{"I am from the future!"}, future_time);

    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    EventCenter::instance().unregisterHandler(scheduled_handle);

    // ===================================================================================
    // STEP 7: Stress Test
    // ===================================================================================
    std::cout << "\n[7] DEMO: Stress Test with multiple concurrent publishers." << std::endl;

    struct StressTestEvent { int id; };

    // Use available hardware concurrency for a realistic test, with a minimum of 2 threads.
    const int NUM_PUBLISHER_THREADS = std::max(2u, std::thread::hardware_concurrency());
    const int EVENTS_PER_THREAD = 20000;
    const int TOTAL_EVENTS = NUM_PUBLISHER_THREADS * EVENTS_PER_THREAD;

    std::atomic<int> received_event_count{0};
    std::mutex stress_test_mutex;
    std::condition_variable stress_test_cv;
    bool all_events_received = false;

    // Register a handler that counts received events and notifies when all are done.
    auto stress_handle = EventCenter::instance().registerHandler<StressTestEvent>(
        [&](const StressTestEvent& event) {
            // Increment the counter. Use relaxed memory order as we only need atomicity, not synchronization.
            int count = received_event_count.fetch_add(1, std::memory_order_relaxed) + 1;
            // If this is the last event, notify the main thread.
            if (count == TOTAL_EVENTS) {
                std::lock_guard<std::mutex> lock(stress_test_mutex);
                all_events_received = true;
                stress_test_cv.notify_one();
            }
        }
    );

    std::cout << "  - Starting " << NUM_PUBLISHER_THREADS << " publisher threads, each sending "
              << EVENTS_PER_THREAD << " events." << std::endl;
    std::cout << "  - Total events to publish: " << TOTAL_EVENTS << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> publishers;
    for (int i = 0; i < NUM_PUBLISHER_THREADS; ++i) {
        publishers.emplace_back([i, EVENTS_PER_THREAD]() {
            for (int j = 0; j < EVENTS_PER_THREAD; ++j) {
                publish_event(StressTestEvent{i * EVENTS_PER_THREAD + j});
            }
        });
    }

    // Wait for all publisher threads to finish queuing their events.
    for (auto& t : publishers) {
        t.join();
    }

    // Wait for the worker thread to process all events.
    std::cout << "  - All events published. Waiting for EventCenter to process..." << std::endl;
    {
        std::unique_lock<std::mutex> lock(stress_test_mutex);
        stress_test_cv.wait(lock, [&]{ return all_events_received; });
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "  - Verification: ";
    if (received_event_count.load() == TOTAL_EVENTS) {
        std::cout << "SUCCESS! Received " << received_event_count.load() << " / " << TOTAL_EVENTS << " events." << std::endl;
        std::cout << "  - Total time for publishing and processing: " << duration.count() << " ms." << std::endl;
    } else {
        std::cout << "FAILURE! Received " << received_event_count.load() << " / " << TOTAL_EVENTS << " events." << std::endl;
    }

    EventCenter::instance().unregisterHandler(stress_handle);

    // --- Finalization ---
    std::cout << "\n--- Demo Finished ---" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
