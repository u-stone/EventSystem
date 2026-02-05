#include "gtest/gtest.h"
#include "EventSystem/EventCenter.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

using namespace eventsystem;

void waitForAsync() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

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

class EventSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        DestroyEventCenter();
        ResetEventCenter();
    }
    void TearDown() override {
        DestroyEventCenter();
    }
};

struct TestEvent1 { int value; };
struct TestEvent2 { std::string value; };

class WeakHandler : public IEventHandler {
public:
    TestSync* sync;
    WeakHandler(TestSync& s) : sync(&s) {}
    void Handle(const std::any& eventData) override {
        if (std::any_cast<TestEvent1>(&eventData)) {
            if (sync) sync->notify();
        }
    }
    ~WeakHandler() { if (sync) sync->notify(); }
};

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

struct StaticEvent {
    static TestSync* sync_ptr;
    static void Handle(const StaticEvent&) {
        if (sync_ptr) sync_ptr->notify();
    }
};
TestSync* StaticEvent::sync_ptr = nullptr;

TEST_F(EventSystemTest, CallbackHandler) {
    TestSync sync;
    auto handle = SubscribeEvent<TestEvent1>([&](const TestEvent1& event) {
        EXPECT_EQ(event.value, 42);
        sync.notify();
    });

    PublishEvent(TestEvent1{42});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));

    TestSync sync2;
    UnsubscribeEvent(handle);
    PublishEvent(TestEvent1{99});
    EXPECT_FALSE(sync2.waitFor(std::chrono::milliseconds(100)));
}

TEST_F(EventSystemTest, StaticHandler) {
    TestSync sync;
    StaticEvent::sync_ptr = &sync;

    auto handle = RegisterStaticEventHandler<StaticEvent>();
    PublishEvent(StaticEvent{});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    
    TestSync sync2;
    StaticEvent::sync_ptr = &sync2;
    UnregisterStaticEventHandler(handle);
    PublishEvent(StaticEvent{});
    EXPECT_FALSE(sync2.waitFor(std::chrono::milliseconds(100)));
    StaticEvent::sync_ptr = nullptr;
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
    
    auto handler = std::make_shared<WeakHandler>(sync_recv);
    SubscribeEventWeak<TestEvent1>(handler);

    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync_recv.waitFor(std::chrono::milliseconds(200)));

    handler->sync = &sync_destroy;
    handler.reset();
    EXPECT_TRUE(sync_destroy.waitFor(std::chrono::milliseconds(200)));
    
    TestSync sync_recv2;
    PublishEvent(TestEvent1{2});
    EXPECT_FALSE(sync_recv2.waitFor(std::chrono::milliseconds(100)));
}

TEST_F(EventSystemTest, StrongHandlerFireAndForget) {
    TestSync sync;

    SubscribeEvent<TestEvent1>(std::make_shared<StrongHandler>(sync));

    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
}

TEST_F(EventSystemTest, UnregisterAll) {
    TestSync sync1, sync2, sync3;
    StaticEvent::sync_ptr = &sync3;

    SubscribeEvent<StaticEvent>(std::make_shared<StrongHandler>(sync1));
    SubscribeEvent<StaticEvent>([&](const StaticEvent&){ sync2.notify(); });
    RegisterStaticEventHandler<StaticEvent>();

    UnsubscribeAllEvents<StaticEvent>();

    PublishEvent(StaticEvent{});
    waitForAsync();

    EXPECT_FALSE(sync1.notified);
    EXPECT_FALSE(sync2.notified);
    EXPECT_FALSE(sync3.notified);

    StaticEvent::sync_ptr = nullptr;
}

TEST_F(EventSystemTest, DelayedEventIsProcessedAfterDelay) {
    TestSync sync;
    const auto delay = std::chrono::milliseconds(200);
    
    std::atomic<std::chrono::steady_clock::time_point> handled_at;

    SubscribeEvent<TestEvent1>([&](const TestEvent1& event) {
        handled_at = std::chrono::steady_clock::now();
        sync.notify();
    });

    auto start_time = std::chrono::steady_clock::now();
    PublishEventDelayed(TestEvent1{100}, delay);

    EXPECT_TRUE(sync.waitFor(delay + std::chrono::milliseconds(100)));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(handled_at.load() - start_time);
    EXPECT_GE(elapsed.count(), delay.count());
}

TEST_F(EventSystemTest, EventsAreProcessedInTemporalOrder) {
    TestSync sync1, sync2, sync3;
    std::vector<int> received_order;
    std::mutex vector_mutex;

    auto handler = [&](const TestEvent1& event) {
        {
            std::lock_guard<std::mutex> lock(vector_mutex);
            received_order.push_back(event.value);
        }
        if (event.value == 1) sync1.notify();
        else if (event.value == 2) sync2.notify();
        else if (event.value == 3) sync3.notify();
    };

    SubscribeEvent<TestEvent1>(handler);

    auto now = std::chrono::steady_clock::now();
    PublishEventAt(TestEvent1{3}, now + std::chrono::milliseconds(300)); 
    PublishEventAt(TestEvent1{1}, now + std::chrono::milliseconds(100)); 
    PublishEventAt(TestEvent1{2}, now + std::chrono::milliseconds(200)); 

    EXPECT_TRUE(sync1.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync2.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(sync3.waitFor(std::chrono::milliseconds(200)));

    ASSERT_EQ(received_order.size(), 3);
    EXPECT_EQ(received_order[0], 1);
    EXPECT_EQ(received_order[1], 2);
    EXPECT_EQ(received_order[2], 3);
}

TEST_F(EventSystemTest, CancelAllEvents) {
    TestSync sync;
    std::atomic<bool> received{false};

    SubscribeEvent<TestEvent1>([&](const TestEvent1&) {
        received = true;
        sync.notify();
    });

    PublishEventDelayed(TestEvent1{999}, std::chrono::milliseconds(200));
    CancelAllEvents();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(received);
    EXPECT_FALSE(sync.notified);

    PublishEvent(TestEvent1{123});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    EXPECT_TRUE(received);
}

TEST_F(EventSystemTest, ExceptionIsolation) {
    TestSync sync;
    
    SubscribeEvent<TestEvent1>([&](const TestEvent1&) {
        throw std::runtime_error("Intentional crash for testing");
    });

    SubscribeEvent<TestEvent1>([&](const TestEvent1&) {
        sync.notify();
    });

    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
}

TEST_F(EventSystemTest, SynchronousMode) {
    bool handled = false;
    std::thread::id handler_thread_id;
    auto main_thread_id = std::this_thread::get_id();

    SubscribeEvent<TestEvent1>([&](const TestEvent1& e) {
        handled = true;
        handler_thread_id = std::this_thread::get_id();
    });

    SetEventCenterPublishMode(PublishMode::Sync);
    PublishEvent(TestEvent1{1});

    EXPECT_TRUE(handled);
    EXPECT_EQ(handler_thread_id, main_thread_id);
}

TEST_F(EventSystemTest, DestructorCancelsPendingEvents) {
    bool executed = false;
    SubscribeEvent<TestEvent1>([&](const TestEvent1&) {
        executed = true;
    });

    PublishEventDelayed(TestEvent1{100}, std::chrono::seconds(2));

    auto start = std::chrono::steady_clock::now();
    DestroyEventCenter(); 
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(elapsed.count(), 200);
    EXPECT_FALSE(executed);
}

TEST_F(EventSystemTest, ConcurrentRegistration) {
    std::atomic<int> received_count{0};
    std::atomic<bool> running{true};
    const int num_publisher_threads = 4;
    const int num_registry_threads = 4;

    std::vector<std::thread> publishers;
    for (int i = 0; i < num_publisher_threads; ++i) {
        publishers.emplace_back([&]() {
            while (running) {
                PublishEvent(TestEvent1{1});
                std::this_thread::yield();
            }
        });
    }

    std::vector<std::thread> registrars;
    for (int i = 0; i < num_registry_threads; ++i) {
        registrars.emplace_back([&]() {
            while (running) {
                auto h = SubscribeEvent<TestEvent1>([&](const TestEvent1&) {
                    received_count.fetch_add(1, std::memory_order_relaxed);
                });
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                UnsubscribeEvent(h);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;

    for (auto& t : publishers) t.join();
    for (auto& t : registrars) t.join();

    TestSync sync;
    auto h = SubscribeEvent<TestEvent1>([&](const TestEvent1&) {
        sync.notify();
    });
    PublishEvent(TestEvent1{1});
    EXPECT_TRUE(sync.waitFor(std::chrono::milliseconds(200)));
    UnsubscribeEvent(h);
}

TEST_F(EventSystemTest, MixedWorkload) {
    std::atomic<int> total_processed{0};
    std::atomic<bool> running{true};
    
    auto main_handler = SubscribeEvent<TestEvent1>([&](const TestEvent1& e) {
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

    EXPECT_GT(total_processed.load(), 0);
    UnsubscribeEvent(main_handler);
}