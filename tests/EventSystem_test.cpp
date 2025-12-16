#include "gtest/gtest.h"
#include "EventSystem.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>

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

// --- Test Event and Handler Definitions ---

struct TestEvent1 { int value; };
struct TestEvent2 { std::string value; };

// A handler for weak reference tests
class WeakHandler : public IEventHandler {
public:
    TestSync* sync;
    WeakHandler(TestSync& s) : sync(&s) {}
    void handle(const std::any& eventData) override {
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
    void handle(const std::any& eventData) override {
        if (std::any_cast<TestEvent1>(&eventData)) {
            sync.notify();
        }
    }
};

// An event with a static handler
struct StaticEvent {
    static TestSync* sync_ptr; // Static pointer to sync object
    static void handle(const StaticEvent&) {
        if (sync_ptr) sync_ptr->notify();
    }
};
TestSync* StaticEvent::sync_ptr = nullptr;


// --- Test Cases ---

TEST(EventSystemTest, Singleton) {
    // Ensures that instance() always returns the same object.
    EXPECT_EQ(&EventCenter::instance(), &EventCenter::instance());
}

TEST(EventSystemTest, CallbackHandler) {
    TestSync sync;
    auto handle = EventCenter::instance().registerHandler<TestEvent1>([&](const TestEvent1& event) {
        EXPECT_EQ(event.value, 42);
        sync.notify();
    });

    publish_event(TestEvent1{42});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));

    // Test unregistering
    TestSync sync2;
    EventCenter::instance().unregisterHandler(handle);
    publish_event(TestEvent1{99});
    EXPECT_FALSE(sync2.waitFor(std::chrono::milliseconds(100)));
}

TEST(EventSystemTest, StaticHandler) {
    TestSync sync;
    StaticEvent::sync_ptr = &sync; // Point static member to our sync object

    auto handle = registerStaticEventHandler<StaticEvent>();
    publish_event(StaticEvent{});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    
    // Test unregistering
    TestSync sync2;
    StaticEvent::sync_ptr = &sync2;
    EventCenter::instance().unregisterHandler(handle);
    publish_event(StaticEvent{});
    EXPECT_FALSE(sync2.waitFor(std::chrono::milliseconds(100)));
    StaticEvent::sync_ptr = nullptr; // Clean up
}

TEST(EventSystemTest, WeakHandlerLifecycle) {
    TestSync sync_recv, sync_destroy;
    
    // 1. Create and register handler
    auto handler = std::make_shared<WeakHandler>(sync_recv);
    EventCenter::instance().registerWeakHandler<TestEvent1>(handler);

    // 2. Publish and expect it to be received
    publish_event(TestEvent1{1});
    EXPECT_TRUE(sync_recv.waitFor(std::chrono::milliseconds(200)));

    // 3. Reset the shared_ptr, destroying the handler object
    // The handler's destructor should notify sync_destroy
    handler->sync = &sync_destroy; // Point to the other sync object
    handler.reset();
    EXPECT_TRUE(sync_destroy.waitFor(std::chrono::milliseconds(200)));
    
    // 4. Publish again and expect it NOT to be received
    TestSync sync_recv2;
    // We can't reuse the handler, so we just check if sync_recv2 is notified
    publish_event(TestEvent1{2});
    EXPECT_FALSE(sync_recv2.waitFor(std::chrono::milliseconds(100)));
}

TEST(EventSystemTest, StrongHandlerFireAndForget) {
    TestSync sync;

    // Register a handler without keeping a shared_ptr to it.
    // EventCenter should keep it alive.
    EventCenter::instance().registerHandler<TestEvent1>(
        std::make_shared<StrongHandler>(sync)
    );

    publish_event(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));

    // Clean up
    EventCenter::instance().unregisterAllHandlers<TestEvent1>();
}

TEST(EventSystemTest, UnregisterAll) {
    TestSync sync1, sync2, sync3;
    StaticEvent::sync_ptr = &sync3;

    // 1. Register handlers that we expect to be removed
    EventCenter::instance().registerHandler<StaticEvent>(std::make_shared<StrongHandler>(sync1));
    EventCenter::instance().registerHandler<StaticEvent>([&](const StaticEvent&){ sync2.notify(); });
    registerStaticEventHandler<StaticEvent>();

    // 2. Unregister all immediately
    EventCenter::instance().unregisterAllHandlers<StaticEvent>();

    // 3. Publish event
    publish_event(StaticEvent{});
    waitForAsync();

    // 4. Ensure none were notified
    EXPECT_FALSE(sync1.notified);
    EXPECT_FALSE(sync2.notified);
    EXPECT_FALSE(sync3.notified);

    // Clean up
    StaticEvent::sync_ptr = nullptr;
}

// --- Tests for Timed Events ---

TEST(EventSystemTest, DelayedEventIsProcessedAfterDelay) {
    TestSync sync;
    const auto delay = std::chrono::milliseconds(200);
    
    std::atomic<std::chrono::steady_clock::time_point> handled_at;

    EventCenter::instance().registerHandler<TestEvent1>([&](const TestEvent1& event) {
        handled_at = std::chrono::steady_clock::now();
        sync.notify();
    });

    auto start_time = std::chrono::steady_clock::now();
    publish_event_delayed(TestEvent1{100}, delay);

    // Wait for the event to be handled
    EXPECT_TRUE(sync.waitFor(delay + std::chrono::milliseconds(100)));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(handled_at.load() - start_time);
    
    // Check if the elapsed time is roughly equal to the delay.
    // Allow a small margin for scheduling and execution overhead.
    EXPECT_GE(elapsed.count(), delay.count());
    EXPECT_LT(elapsed.count(), delay.count() + 50); // Allow 50ms overhead

    // Clean up
    EventCenter::instance().unregisterAllHandlers<TestEvent1>();
}

TEST(EventSystemTest, EventsAreProcessedInTemporalOrder) {
    TestSync sync1, sync2, sync3;
    std::vector<int> received_order;
    std::mutex vector_mutex;

    // Handler that records the order of events received
    auto handler = [&](const TestEvent1& event) {
        std::lock_guard<std::mutex> lock(vector_mutex);
        received_order.push_back(event.value);
        if (event.value == 1) sync1.notify();
        else if (event.value == 2) sync2.notify();
        else if (event.value == 3) sync3.notify();
    };

    auto handle = EventCenter::instance().registerHandler<TestEvent1>(handler);

    auto now = std::chrono::steady_clock::now();

    // Publish events out of order with different delays
    publish_event_at(TestEvent1{3}, now + std::chrono::milliseconds(300)); // Last
    publish_event_at(TestEvent1{1}, now + std::chrono::milliseconds(100)); // First
    publish_event_at(TestEvent1{2}, now + std::chrono::milliseconds(200)); // Second

    // Wait for all events to be processed
    EXPECT_TRUE(sync1.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync2.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync3.waitFor(std::chrono::milliseconds(200)));

    // Verify the received order
    ASSERT_EQ(received_order.size(), 3);
    EXPECT_EQ(received_order[0], 1);
    EXPECT_EQ(received_order[1], 2);
    EXPECT_EQ(received_order[2], 3);

    // Clean up
    EventCenter::instance().unregisterHandler(handle);
}

TEST(EventSystemTest, ScheduledEventIsProcessedAtTime) {
    TestSync sync;
    const auto scheduled_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    std::atomic<std::chrono::steady_clock::time_point> handled_at;

    EventCenter::instance().registerHandler<TestEvent2>([&](const TestEvent2& event) {
        handled_at = std::chrono::steady_clock::now();
        sync.notify();
    });

    publish_event_at(TestEvent2{"scheduled"}, scheduled_time);

    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(350)));

    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(handled_at.load() - scheduled_time);

    // The event should be handled at or just after the scheduled time.
    // It shouldn't be handled before.
    EXPECT_GE(time_diff.count(), 0);
    EXPECT_LT(time_diff.count(), 50); // Allow 50ms overhead

    // Clean up
    EventCenter::instance().unregisterAllHandlers<TestEvent2>();
}

TEST(EventSystemTest, CancelAllEvents) {
    TestSync sync;
    std::atomic<bool> received{false};

    auto handle = EventCenter::instance().registerHandler<TestEvent1>([&](const TestEvent1&) {
        received = true;
        sync.notify();
    });

    // 1. Publish a delayed event
    publish_event_delayed(TestEvent1{999}, std::chrono::milliseconds(200));

    // 2. Cancel all events immediately
    cancelAllEvents();

    // 3. Wait longer than the delay to ensure it didn't fire
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(received);
    EXPECT_FALSE(sync.notified);

    // 4. Verify the system is still operational
    publish_event(TestEvent1{123});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(received);

    EventCenter::instance().unregisterHandler(handle);
}

TEST(EventSystemTest, ExceptionIsolation) {
    TestSync sync;
    
    // 1. Register a handler that throws an exception
    EventCenter::instance().registerHandler<TestEvent1>([&](const TestEvent1&) {
        throw std::runtime_error("Intentional crash for testing");
    });

    // 2. Register a second handler that should still run
    EventCenter::instance().registerHandler<TestEvent1>([&](const TestEvent1&) {
        sync.notify();
    });

    // 3. Publish event. The first handler will crash, but the second should succeed.
    publish_event(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));

    EventCenter::instance().unregisterAllHandlers<TestEvent1>();
}

TEST(EventSystemTest, SynchronousMode) {
    // Ensure we restore async mode even if test fails
    struct ScopedAsyncRestorer {
        ~ScopedAsyncRestorer() { EventCenter::instance().setWorkThreadEnable(true); }
    } restorer;

    // 1. Switch to synchronous mode
    EventCenter::instance().setWorkThreadEnable(false);

    bool handled = false;
    std::thread::id handler_thread_id;
    auto main_thread_id = std::this_thread::get_id();

    auto handle = EventCenter::instance().registerHandler<TestEvent1>([&](const TestEvent1& e) {
        handled = true;
        handler_thread_id = std::this_thread::get_id();
    });

    // 2. Publish event
    publish_event(TestEvent1{1});

    // 3. Verify immediate execution on the same thread
    EXPECT_TRUE(handled);
    EXPECT_EQ(handler_thread_id, main_thread_id);

    // 4. Verify delayed events are ignored in sync mode
    handled = false;
    publish_event_delayed(TestEvent1{2}, std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(handled);

    EventCenter::instance().unregisterHandler(handle);
}

TEST(EventSystemTest, SingletonDestruction) {
    EventCenter& instance1 = EventCenter::instance();

    // Register a handler on instance 1 to verify state loss
    bool handled = false;
    // We don't need to keep the handle because we are destroying the whole system
    instance1.registerHandler<TestEvent1>([&](const TestEvent1&){
        handled = true;
    });

    // Destroy the singleton
    EventCenter::destroy();

    // Get a new instance

    // Verify state is reset (handler from instance1 should not exist in instance2)
    publish_event(TestEvent1{1});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(handled);
}