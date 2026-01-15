#include <gtest/gtest.h>
#include "EventSystem/MessageCenter.h"
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

using namespace eventsystem;

class MessageCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear state if necessary
    }
};

TEST_F(MessageCenterTest, BasicSubscriptionAndPublish) {
    // Subscribe using the unified static method (or helper)
    std::string received_msg;
    auto token = MessageRegistry::subscribe("user_login", [&](const std::string& msg) {
        received_msg = msg;
    });

    // Publish using SyncCenter
    SyncMessageCenter::instance().publish("user_login", "Alice");
    EXPECT_EQ(received_msg, "Alice");

    MessageRegistry::unsubscribe("user_login", token);
}

TEST_F(MessageCenterTest, MultipleSubscribers) {
    std::atomic<int> count{0};

    auto token1 = MessageRegistry::subscribe("broadcast", [&](const std::string&) { count++; });
    auto token2 = MessageRegistry::subscribe("broadcast", [&](const std::string&) { count++; });

    SyncMessageCenter::instance().publish("broadcast", "hello");
    EXPECT_EQ(count.load(), 2);

    MessageRegistry::unsubscribe("broadcast", token1);
    MessageRegistry::unsubscribe("broadcast", token2);
}

TEST_F(MessageCenterTest, UnsubscribeEffectiveness) {
    int count = 0;

    auto token = MessageRegistry::subscribe("temp_topic", [&](const std::string&) { count++; });
    SyncMessageCenter::instance().publish("temp_topic", "first");
    EXPECT_EQ(count, 1);

    MessageRegistry::unsubscribe("temp_topic", token);
    SyncMessageCenter::instance().publish("temp_topic", "second");
    EXPECT_EQ(count, 1); // Should remain 1
}

TEST_F(MessageCenterTest, ExceptionIsolation) {
    bool second_called = false;

    MessageRegistry::subscribe("fail_topic", [](const std::string&) { throw std::runtime_error("Intentional"); });
    MessageRegistry::subscribe("fail_topic", [&](const std::string&) { second_called = true; });

    EXPECT_NO_THROW(SyncMessageCenter::instance().publish("fail_topic", "test"));
    EXPECT_TRUE(second_called);
}

TEST_F(MessageCenterTest, AsyncPublish) {
    std::atomic<bool> received{false};

    auto token = MessageRegistry::subscribe("async_topic", [&](const std::string& msg) {
        if (msg == "ping") received = true;
    });

    AsyncMessageCenter::instance().publish("async_topic", "ping");
    
    // Wait a bit for the worker thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(received);
    MessageRegistry::unsubscribe("async_topic", token);
}

TEST_F(MessageCenterTest, SharedRegistry) {
    // Verify that a subscription receives messages from BOTH Sync and Async centers
    std::atomic<int> count{0};
    auto token = MessageRegistry::subscribe("shared_topic", [&](const std::string&) { count++; });

    SyncMessageCenter::instance().publish("shared_topic", "sync");
    AsyncMessageCenter::instance().publish("shared_topic", "async");

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait for async

    EXPECT_EQ(count.load(), 2);
    MessageRegistry::unsubscribe("shared_topic", token);
}

TEST_F(MessageCenterTest, UnsubscribeAllForTopic) {
    std::atomic<int> count{0};

    // Register multiple subscribers for the same topic
    MessageRegistry::subscribe("bulk_topic", [&](const std::string&) { count++; });
    MessageRegistry::subscribe("bulk_topic", [&](const std::string&) { count++; });
    MessageRegistry::subscribe("other_topic", [&](const std::string&) { count++; }); // Should not be affected

    // 1. Publish to verify all receive it
    SyncMessageCenter::instance().publish("bulk_topic", "msg1");
    EXPECT_EQ(count.load(), 2);

    // 2. Unsubscribe ALL for "bulk_topic"
    MessageRegistry::unsubscribe("bulk_topic");

    // 3. Publish again - should be 0 increments for bulk_topic
    SyncMessageCenter::instance().publish("bulk_topic", "msg2");
    EXPECT_EQ(count.load(), 2); // Count remains 2

    // 4. Verify "other_topic" is still active
    SyncMessageCenter::instance().publish("other_topic", "msg3");
    EXPECT_EQ(count.load(), 3); // Count increments to 3
}

// --- Multi-threaded Tests ---

TEST_F(MessageCenterTest, ConcurrentSubscribePublish) {
    // Heavy concurrent load: multiple threads subscribing and publishing to the same topic
    const int num_threads = 10;
    const int ops_per_thread = 1000;
    std::atomic<int> received_count{0};
    std::atomic<bool> running{true};

    // Subscriber thread: keeps adding and removing subscribers
    std::thread subscriber([&]() {
        while (running) {
            auto token = MessageRegistry::subscribe("concurrent_topic", [&](const std::string&) {
                received_count.fetch_add(1, std::memory_order_relaxed);
            });
            // Yield to increase contention
            std::this_thread::yield(); 
            MessageRegistry::unsubscribe("concurrent_topic", token);
        }
    });

    // Publisher threads
    std::vector<std::thread> publishers;
    for (int i = 0; i < num_threads; ++i) {
        publishers.emplace_back([&]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                // Use Sync to stress the lock immediately
                SyncMessageCenter::instance().publish("concurrent_topic", "stress");
            }
        });
    }

    for (auto& t : publishers) t.join();
    running = false;
    subscriber.join();

    // Verification is mostly that it didn't crash
    // received_count will be non-deterministic, which is expected
    SUCCEED();
}

TEST_F(MessageCenterTest, ConcurrentAsyncPublish) {
    const int num_publishers = 8;
    const int msgs_per_publisher = 1000;
    const int total_msgs = num_publishers * msgs_per_publisher;
    
    std::atomic<int> count{0};
    auto token = MessageRegistry::subscribe("async_stress", [&](const std::string&) {
        count.fetch_add(1, std::memory_order_relaxed);
    });

    std::vector<std::thread> threads;
    for (int i = 0; i < num_publishers; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < msgs_per_publisher; ++j) {
                AsyncMessageCenter::instance().publish("async_stress", "data");
            }
        });
    }

    for (auto& t : threads) t.join();

    // Wait for processing
    int retries = 0;
    while (count.load() < total_msgs && retries++ < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_EQ(count.load(), total_msgs);
    MessageRegistry::unsubscribe("async_stress", token);
}
