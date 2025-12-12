#include "EventSystem.h"
#include <string>
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>

// We will use a simple event for the stress test.
struct StressTestEvent {
    int sequenceNumber;
};

// A dedicated handler to count received events for verification.
class StressTestHandler : public IEventHandler {
public:
    // Use an atomic counter for thread-safe increments.
    std::atomic<int> m_eventsProcessed{0};

    void handle(const std::any& eventData) override {
        if (std::any_cast<StressTestEvent>(&eventData)) {
            m_eventsProcessed++;
        }
    }
};

// This function will be executed by each producer thread.
// It no longer needs an EventSource reference.
void event_producer_task(int events_to_post) {
    for (int i = 0; i < events_to_post; ++i) {
        // Use the new helper function to publish events from anywhere.
        publish_event(StressTestEvent{i});
    }
}

int main() {
    std::cout << "--- Initializing Stress Test with Singleton EventCenter ---" << std::endl;

    // Create a handler.
    StressTestHandler stressHandler;
    // Register the handler using the singleton instance.
    EventCenter::instance().registerHandler<StressTestEvent>(&stressHandler);

    // --- Test Parameters ---
    const int num_threads = std::thread::hardware_concurrency();
    const int events_per_thread = 25000;
    const int total_events = num_threads * events_per_thread;
    
    std::cout << "Starting test with " << num_threads << " producer threads." << std::endl;
    std::cout << "Each thread will post " << events_per_thread << " events." << std::endl;
    std::cout << "Total events to be processed: " << total_events << std::endl;
    std::cout << "------------------------------------" << std::endl;


    // --- Start Producer Threads ---
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> producer_threads;
    for (int i = 0; i < num_threads; ++i) {
        // The task now only needs to know how many events to post.
        producer_threads.emplace_back(event_producer_task, events_per_thread);
    }

    for (auto& t : producer_threads) {
        t.join();
    }

    auto producers_finished_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> producer_duration = producers_finished_time - start_time;
    std::cout << "All " << num_threads << " producer threads finished posting in " 
              << producer_duration.count() << " seconds." << std::endl;


    // --- Wait for Consumer to Catch Up ---
    std::cout << "Waiting for EventCenter's worker thread to process all events..." << std::endl;
    while (stressHandler.m_eventsProcessed.load(std::memory_order_relaxed) < total_events) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto consumer_finished_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> consumer_duration = consumer_finished_time - producers_finished_time;
    std::cout << "EventCenter finished processing all events in an additional " 
              << consumer_duration.count() << " seconds." << std::endl;


    // --- Verification ---
    std::cout << "\n--- Stress Test Results ---" << std::endl;
    std::cout << "Expected events processed: " << total_events << std::endl;
    std::cout << "Actual events processed:   " << stressHandler.m_eventsProcessed.load() << std::endl;

    if (stressHandler.m_eventsProcessed.load() == total_events) {
        std::cout << "\nSUCCESS: The EventCenter correctly processed all events under load." << std::endl;
    } else {
        std::cout << "\nFAILURE: There is a mismatch in the event count. Events were lost or corrupted." << std::endl;
    }

    // The EventCenter singleton will be destroyed here as the program exits.
    return 0;
}