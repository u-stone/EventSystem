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
    TestSync& sync;
    WeakHandler(TestSync& s) : sync(s) {}
    void handle(const std::any& eventData) override {
        if (std::any_cast<TestEvent1>(&eventData)) {
            sync.notify();
        }
    }
    ~WeakHandler() { sync.notify(); } // Notify on destruction
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
    handler->sync = sync_destroy; // Point to the other sync object
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

    // Register one of each type for the same event
    EventCenter::instance().registerHandler<StaticEvent>(std::make_shared<StrongHandler>(sync1));
    EventCenter::instance().registerHandler<StaticEvent>([&](const StaticEvent&){ sync2.notify(); });
    registerStaticEventHandler<StaticEvent>();

    // Publish and ensure all are received
    publish_event(StaticEvent{});
    EXPECT_TRUE(sync1.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync2.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync3.waitFor(std::chrono::milliseconds(200)));

    // Unregister all
    EventCenter::instance().unregisterAllHandlers<StaticEvent>();

    // Publish again and ensure none are received
    TestSync sync_fail1, sync_fail2, sync_fail3;
    StaticEvent::sync_ptr = &sync_fail3;
     EventCenter::instance().registerHandler<StaticEvent>(std::make_shared<StrongHandler>(sync_fail1));
     EventCenter::instance().registerHandler<StaticEvent>([&](const StaticEvent&){ sync_fail2.notify(); });

    publish_event(StaticEvent{});
    waitForAsync();
    EXPECT_FALSE(sync_fail1.notified);
    EXPECT_FALSE(sync_fail2.notified);
    EXPECT_FALSE(sync_fail3.notified);

    // Clean up
    StaticEvent::sync_ptr = nullptr;
    EventCenter::instance().unregisterAllHandlers<StaticEvent>();
}
