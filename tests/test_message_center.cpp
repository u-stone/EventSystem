#include <gtest/gtest.h>
#include "EventSystem/MessageCenter.h"
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

using namespace eventsystem;

class MessageCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear state if necessary, though singleton is used
    }
};

TEST_F(MessageCenterTest, BasicSubscriptionAndPublish) {
    auto& mc = SyncMessageCenter::instance();
    std::string received_msg;
    
    auto token = mc.subscribe("user_login", [&](const std::string& msg) {
        received_msg = msg;
    });

    mc.publish("user_login", "Alice");
    EXPECT_EQ(received_msg, "Alice");

    mc.unsubscribe("user_login", token);
}

TEST_F(MessageCenterTest, MultipleSubscribers) {
    auto& mc = SyncMessageCenter::instance();
    std::atomic<int> count{0};

    auto token1 = mc.subscribe("broadcast", [&](const std::string&) { count++; });
    auto token2 = mc.subscribe("broadcast", [&](const std::string&) { count++; });

    mc.publish("broadcast", "hello");
    EXPECT_EQ(count.load(), 2);

    mc.unsubscribe("broadcast", token1);
    mc.unsubscribe("broadcast", token2);
}

TEST_F(MessageCenterTest, UnsubscribeEffectiveness) {
    auto& mc = SyncMessageCenter::instance();
    int count = 0;

    auto token = mc.subscribe("temp_topic", [&](const std::string&) { count++; });
    mc.publish("temp_topic", "first");
    EXPECT_EQ(count, 1);

    mc.unsubscribe("temp_topic", token);
    mc.publish("temp_topic", "second");
    EXPECT_EQ(count, 1); // Should remain 1
}

TEST_F(MessageCenterTest, ExceptionIsolation) {
    auto& mc = SyncMessageCenter::instance();
    bool second_called = false;

    mc.subscribe("fail_topic", [](const std::string&) { throw std::runtime_error("Intentional"); });
    mc.subscribe("fail_topic", [&](const std::string&) { second_called = true; });

    EXPECT_NO_THROW(mc.publish("fail_topic", "test"));
    EXPECT_TRUE(second_called);
}

TEST_F(MessageCenterTest, AsyncPublish) {
    auto& mc = AsyncMessageCenter::instance(); // Use the async instance
    std::atomic<bool> received{false};

    auto token = mc.subscribe("async_topic", [&](const std::string& msg) {
        if (msg == "ping") received = true;
    });

    mc.publish("async_topic", "ping");
    
    // Wait a bit for the worker thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(received);
    mc.unsubscribe("async_topic", token);
}