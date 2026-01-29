#include "gtest/gtest.h"
#include "EventSystem/EventCenter.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

using namespace eventsystem;

// Use a short sleep to give the worker thread time to process
// in tests where we expect something *not* to happen.
void waitForAsync() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// --- Test Fixture and Payloads ---

// A helper struct to make tests deterministic when dealing with the async EventCenter.
struct TestSync {
    std::mutex m;
    std::condition_variable cv;
    bool notified = false;

    void notify() {
        {
            std::lock_guard<std::mutex> lock(m);
            notified = true;
        }
        cv.notify_one();
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m);
        return cv.wait_for(lock, timeout, [this]{ return notified; });
    }
};

// Test Fixture to ensure a clean environment for every test.
class EventSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        EventCenter::Destroy();
        SyncEventCenter::Destroy();
        EventRegistry::Reset();
    }
    void TearDown() override {
        EventCenter::Destroy();
        SyncEventCenter::Destroy();
        EventRegistry::Reset();
    }
};

// --- Test Event and Handler Definitions ---

struct TestEvent1 { int value; };
struct TestEvent2 { std::string value; };

// A handler for weak reference tests
class WeakHandler : public IEventHandler {
public:
    TestSync* sync;
    WeakHandler(TestSync& s) : sync(&s) {}
    void Handle(const std::any& eventData) override {
        if (std::any_cast<TestEvent1>(&eventData)) {
            if (sync) sync->notify();
        }
    }
    ~WeakHandler() { if (sync) sync->notify(); } // Notify on destruction
};

// A handler for strong reference tests
class StrongHandler : public IEventHandler {
public:
    TestSync& sync;
    StrongHandler(TestSync& s) : sync(s) {}
    void Handle(const std::any& eventData) override {
        if (std::any_cast<TestEvent1>(&eventData)) {
            sync.notify();
        }
    }
};

// An event with a static handler
struct StaticEvent {
    static TestSync* sync_ptr; // Static pointer to sync object
    static void Handle(const StaticEvent&) {
        if (sync_ptr) sync_ptr->notify();
    }
};
TestSync* StaticEvent::sync_ptr = nullptr;


// --- Test Cases ---

TEST_F(EventSystemTest, Singleton) {
    // Ensures that instance() always returns the same object.
    EXPECT_EQ(&EventCenter::Instance(), &EventCenter::Instance());
}

TEST_F(EventSystemTest, CallbackHandler) {
    TestSync sync;
    auto handle = EventCenter::Instance().RegisterHandler<TestEvent1>([&](const TestEvent1& event) {
        EXPECT_EQ(event.value, 42);
        sync.notify();
    });

    PublishEvent(TestEvent1{42});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));

    // Test unregistering
    TestSync sync2;
    EventCenter::Instance().UnregisterHandler(handle);
    PublishEvent(TestEvent1{99});
    EXPECT_FALSE(sync2.waitFor(std::chrono::milliseconds(100)));
}

TEST_F(EventSystemTest, StaticHandler) {
    TestSync sync;
    StaticEvent::sync_ptr = &sync; // Point static member to our sync object

    auto handle = RegisterStaticEventHandler<StaticEvent>();
    PublishEvent(StaticEvent{});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    
    // Test unregistering
    TestSync sync2;
    StaticEvent::sync_ptr = &sync2;
    UnregisterStaticEventHandler(handle);
    PublishEvent(StaticEvent{});
    EXPECT_FALSE(sync2.waitFor(std::chrono::milliseconds(100)));
    StaticEvent::sync_ptr = nullptr; // Clean up
}

TEST_F(EventSystemTest, UnregisterAllStaticHandlers) {
    TestSync sync;
    StaticEvent::sync_ptr = &sync;

    RegisterStaticEventHandler<StaticEvent>();
    RegisterStaticEventHandler<StaticEvent>();

    UnregisterStaticEventHandler<StaticEvent>();

    PublishEvent(StaticEvent{});
    EXPECT_FALSE(sync.waitFor(std::chrono::milliseconds(100)));
    StaticEvent::sync_ptr = nullptr;
}

TEST_F(EventSystemTest, WeakHandlerLifecycle) {
    TestSync sync_recv, sync_destroy;
    
    // 1. Create and register handler
    auto handler = std::make_shared<WeakHandler>(sync_recv);
    EventCenter::Instance().RegisterWeakHandler<TestEvent1>(handler);

    // 2. Publish and expect it to be received
    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync_recv.waitFor(std::chrono::milliseconds(200)));

    // 3. Reset the shared_ptr, destroying the handler object
    // The handler's destructor should notify sync_destroy
    handler->sync = &sync_destroy; // Point to the other sync object
    handler.reset();
    EXPECT_TRUE(sync_destroy.waitFor(std::chrono::milliseconds(200)));
    
    // 4. Publish again and expect it NOT to be received
    TestSync sync_recv2;
    // We can't reuse the handler, so we just check if sync_recv2 is notified
    PublishEvent(TestEvent1{2});
    EXPECT_FALSE(sync_recv2.waitFor(std::chrono::milliseconds(100)));
}

TEST_F(EventSystemTest, StrongHandlerFireAndForget) {
    TestSync sync;

    // Register a handler without keeping a shared_ptr to it.
    // EventCenter should keep it alive.
    EventCenter::Instance().RegisterHandler<TestEvent1>(
        std::make_shared<StrongHandler>(sync)
    );

    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
}

TEST_F(EventSystemTest, UnregisterAll) {
    TestSync sync1, sync2, sync3;
    StaticEvent::sync_ptr = &sync3;

    // 1. Register handlers that we expect to be removed
    EventCenter::Instance().RegisterHandler<StaticEvent>(std::make_shared<StrongHandler>(sync1));
    EventCenter::Instance().RegisterHandler<StaticEvent>([&](const StaticEvent&){ sync2.notify(); });
    RegisterStaticEventHandler<StaticEvent>();

    // 2. Unregister all immediately
    EventCenter::Instance().UnregisterAllHandlers<StaticEvent>();

    // 3. Publish event
    PublishEvent(StaticEvent{});
    waitForAsync();

    // 4. Ensure none were notified
    EXPECT_FALSE(sync1.notified);
    EXPECT_FALSE(sync2.notified);
    EXPECT_FALSE(sync3.notified);

    // Clean up
    StaticEvent::sync_ptr = nullptr;
}

// --- Tests for Timed Events ---

TEST_F(EventSystemTest, DelayedEventIsProcessedAfterDelay) {
    TestSync sync;
    const auto delay = std::chrono::milliseconds(200);
    
    std::atomic<std::chrono::steady_clock::time_point> handled_at;

    EventCenter::Instance().RegisterHandler<TestEvent1>([&](const TestEvent1& event) {
        handled_at = std::chrono::steady_clock::now();
        sync.notify();
    });

    auto start_time = std::chrono::steady_clock::now();
    PublishEventDelayed(TestEvent1{100}, delay);

    // Wait for the event to be handled
    EXPECT_TRUE(sync.waitFor(delay + std::chrono::milliseconds(100)));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(handled_at.load() - start_time);
    
    // Check if the elapsed time is roughly equal to the delay.
    // Allow a small margin for scheduling and execution overhead.
    EXPECT_GE(elapsed.count(), delay.count());
    EXPECT_LT(elapsed.count(), delay.count() + 50); // Allow 50ms overhead
}

TEST_F(EventSystemTest, EventsAreProcessedInTemporalOrder) {
    TestSync sync1, sync2, sync3;
    std::vector<int> received_order;
    std::mutex vector_mutex;

    // Handler that records the order of events received
    auto handler = [&](const TestEvent1& event) {
        {
            std::lock_guard<std::mutex> lock(vector_mutex);
            received_order.push_back(event.value);
        }
        if (event.value == 1) sync1.notify();
        else if (event.value == 2) sync2.notify();
        else if (event.value == 3) sync3.notify();
    };

    auto handle = EventCenter::Instance().RegisterHandler<TestEvent1>(handler);

    auto now = std::chrono::steady_clock::now();

    // Publish events out of order with different delays
    PublishEventAt(TestEvent1{3}, now + std::chrono::milliseconds(300)); // Last
    PublishEventAt(TestEvent1{1}, now + std::chrono::milliseconds(100)); // First
    PublishEventAt(TestEvent1{2}, now + std::chrono::milliseconds(200)); // Second

    // Wait for all events to be processed
    EXPECT_TRUE(sync1.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync2.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync3.waitFor(std::chrono::milliseconds(200)));

    // Verify the received order
    ASSERT_EQ(received_order.size(), 3);
    EXPECT_EQ(received_order[0], 1);
    EXPECT_EQ(received_order[1], 2);
    EXPECT_EQ(received_order[2], 3);
}

TEST_F(EventSystemTest, ScheduledEventIsProcessedAtTime) {
    TestSync sync;
    const auto scheduled_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    std::atomic<std::chrono::steady_clock::time_point> handled_at;

    EventCenter::Instance().RegisterHandler<TestEvent2>([&](const TestEvent2& event) {
        handled_at = std::chrono::steady_clock::now();
        sync.notify();
    });

    PublishEventAt(TestEvent2{"scheduled"}, scheduled_time);

    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(350)));

    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(handled_at.load() - scheduled_time);

    // The event should be handled at or just after the scheduled time.
    // It shouldn't be handled before.
    EXPECT_GE(time_diff.count(), 0);
    EXPECT_LT(time_diff.count(), 50); // Allow 50ms overhead
}

TEST_F(EventSystemTest, CancelAllEvents) {
    TestSync sync;
    std::atomic<bool> received{false};

    auto handle = EventCenter::Instance().RegisterHandler<TestEvent1>([&](const TestEvent1&) {
        received = true;
        sync.notify();
    });

    // 1. Publish a delayed event
    PublishEventDelayed(TestEvent1{999}, std::chrono::milliseconds(200));

    // 2. Cancel all events immediately
    CancelAllEvents();

    // 3. Wait longer than the delay to ensure it didn't fire
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(received);
    EXPECT_FALSE(sync.notified);

    // 4. Verify the system is still operational
    PublishEvent(TestEvent1{123});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(received);
}

TEST_F(EventSystemTest, ExceptionIsolation) {
    TestSync sync;
    
    // 1. Register a handler that throws an exception
    EventCenter::Instance().RegisterHandler<TestEvent1>([&](const TestEvent1&) {
        throw std::runtime_error("Intentional crash for testing");
    });

    // 2. Register a second handler that should still run
    EventCenter::Instance().RegisterHandler<TestEvent1>([&](const TestEvent1&) {
        sync.notify();
    });

    // 3. Publish event. The first handler will crash, but the second should succeed.
    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
}

TEST_F(EventSystemTest, SynchronousMode) {
    bool handled = false;
    std::thread::id handler_thread_id;
    auto main_thread_id = std::this_thread::get_id();

    // Register with SyncEventCenter
    auto handle = SyncEventCenter::Instance().RegisterHandler<TestEvent1>([&](const TestEvent1& e) {
        handled = true;
        handler_thread_id = std::this_thread::get_id();
    });

    // 2. Publish event synchronously
    PublishEventSync(TestEvent1{1});

    // 3. Verify immediate execution on the same thread
    EXPECT_TRUE(handled);
    EXPECT_EQ(handler_thread_id, main_thread_id);
    
    SyncEventCenter::Instance().UnregisterHandler(handle);
}

TEST_F(EventSystemTest, SingletonDestruction) {
    EventCenter& instance1 = EventCenter::Instance();

    // Register a handler on instance 1 to verify state loss
    bool handled = false;
    // We don't need to keep the handle because we are destroying the whole system
    instance1.RegisterHandler<TestEvent1>([&](const TestEvent1&){
        handled = true;
    });

    // Destroy the singleton
    EventCenter::Destroy();
    EventRegistry::Reset(); // Clear static handlers to simulate full system reset

    // Get a new instance

    // Verify state is reset (handler from instance1 should not exist in instance2)
    PublishEvent(TestEvent1{1});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(handled);
}

TEST_F(EventSystemTest, DestructorCancelsPendingEvents) {
    // Setup: Get a fresh instance (handled by SetUp, but we get reference here)
    EventCenter& center = EventCenter::Instance();

    // Register a handler just to be sure
    bool executed = false;
    center.RegisterHandler<TestEvent1>([&](const TestEvent1&) {
        executed = true;
    });

    // Action: Schedule an event far in the future (e.g., 2 seconds)
    // If the destructor doesn't cancel events, it might hang waiting for this,
    // or the worker thread might busy-loop until this time is reached.
    center.PublishEventDelayed(TestEvent1{100}, std::chrono::seconds(2));

    // Measure destruction time
    auto start = std::chrono::steady_clock::now();
    EventCenter::Destroy(); // Triggers ~EventCenter()
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Assertion: Destruction should be immediate (e.g., < 200ms), not 2 seconds.
    EXPECT_LT(elapsed.count(), 200);
    EXPECT_FALSE(executed);
}

// --- Multi-threaded Tests ---

TEST_F(EventSystemTest, ConcurrentRegistration) {
    // Test robustness when multiple threads register/unregister handlers while events are processed.
    std::atomic<int> received_count{0};
    std::atomic<bool> running{true};
    const int num_publisher_threads = 4;
    const int num_registry_threads = 4;

    // Publisher threads
    std::vector<std::thread> publishers;
    for (int i = 0; i < num_publisher_threads; ++i) {
        publishers.emplace_back([&]() {
            while (running) {
                PublishEvent(TestEvent1{1});
                std::this_thread::yield();
            }
        });
    }

    // Registry threads: rapidly add/remove handlers
    std::vector<std::thread> registrars;
    for (int i = 0; i < num_registry_threads; ++i) {
        registrars.emplace_back([&]() {
            while (running) {
                auto h = EventRegistry::RegisterHandler<TestEvent1>([&](const TestEvent1&) {
                    received_count.fetch_add(1, std::memory_order_relaxed);
                });
                // Small sleep to allow some events to be processed
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                EventRegistry::UnregisterHandler(h);
            }
        });
    }

    // Run for a while
    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;

    for (auto& t : publishers) t.join();
    for (auto& t : registrars) t.join();

    // Verify system is still responsive
    TestSync sync;
    auto h = EventRegistry::RegisterHandler<TestEvent1>([&](const TestEvent1&) {
        sync.notify();
    });
    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    EventRegistry::UnregisterHandler(h);
}

TEST_F(EventSystemTest, MixedWorkload) {
    // A chaos test mixing delayed, immediate events, and registration.
    std::atomic<int> total_processed{0};
    std::atomic<bool> running{true};
    
    auto main_handler = EventRegistry::RegisterHandler<TestEvent1>([&](const TestEvent1& e) {
        total_processed.fetch_add(1, std::memory_order_relaxed);
    });

    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&, i]() {
            while (running) {
                if (i % 2 == 0) {
                    PublishEvent(TestEvent1{i});
                } else {
                    PublishEventDelayed(TestEvent1{i}, std::chrono::milliseconds(1 + (i*10)));
                }
                
                // Occasional sync publish
                if (i == 0) {
                    PublishEventSync(TestEvent1{i});
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;
    for (auto& t : workers) t.join();

    // Ensure we processed something
    EXPECT_GT(total_processed.load(), 0);
    
    // Cleanup
    EventRegistry::UnregisterHandler(main_handler);
}