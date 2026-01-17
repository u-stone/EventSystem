#include <gtest/gtest.h>
#include "EventSystem/MessageCenter.h"
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional> // For std::bind

using namespace eventsystem;

class MessageCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No explicit reset needed as we use different topics usually
    }
};

namespace {
    // Helper for Free Function Pointer test
    int g_freeFuncVal = 0;
    void freeFunc(int i) { g_freeFuncVal = i; }

    // Helper for Functor test
    struct MyFunctor {
        int* target;
        void operator()(int i) { *target = i; }
    };

    // Helper for Member Function test
    struct MemberMethod {
        int& out;
        void memberFunc(int i) { out = i; }
    };
}

TEST_F(MessageCenterTest, BasicString) {
    std::string result;
    auto token = subscribe_message<std::string>("test_str", [&](const std::string& s) {
        result = s;
    });

    publish_message_sync("test_str", std::string("Hello"));
    EXPECT_EQ(result, "Hello");
    unsubscribe_message("test_str", token);
}

TEST_F(MessageCenterTest, ZeroArgs) {
    bool called = false;
    auto token = subscribe_message<>("test_void", [&]() {
        called = true;
    });

    publish_message_sync("test_void");
    EXPECT_TRUE(called);
    unsubscribe_message("test_void", token);
}

TEST_F(MessageCenterTest, MultiArgs) {
    int v1 = 0;
    float v2 = 0.0f;
    std::string v3;

    auto token = subscribe_message<int, float, std::string>("test_multi", 
        [&](int i, float f, std::string s) {
            v1 = i; v2 = f; v3 = s;
        });

    publish_message_sync("test_multi", 42, 3.14f, std::string("Pi"));
    
    EXPECT_EQ(v1, 42);
    EXPECT_FLOAT_EQ(v2, 3.14f);
    EXPECT_EQ(v3, "Pi");

    unsubscribe_message("test_multi", token);
}

TEST_F(MessageCenterTest, TypeMismatchIgnored) {
    bool called = false;
    auto token = subscribe_message<int>("test_mismatch", [&](int) {
        called = true;
    });

    // Publish float, subscriber expects int. Should NOT call.
    publish_message_sync("test_mismatch", 3.14f);
    EXPECT_FALSE(called);

    // Publish int
    publish_message_sync("test_mismatch", 100);
    EXPECT_TRUE(called);

    unsubscribe_message("test_mismatch", token);
}

TEST_F(MessageCenterTest, AsyncMultiArgs) {
    std::atomic<int> sum{0};
    auto token = subscribe_message<int, int>("test_async_add", [&](int a, int b) {
        sum = a + b;
    });

    publish_message_async("test_async_add", 10, 20);
    
    int retries = 0;
    while(sum == 0 && retries++ < 10) std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(sum, 30);
    unsubscribe_message("test_async_add", token);
}

TEST_F(MessageCenterTest, UnsubscribeAll) {
    int count = 0;
    subscribe_message<int>("bulk", [&](int){ count++; });
    subscribe_message<int>("bulk", [&](int){ count++; });

    publish_message_sync("bulk", 1);
    EXPECT_EQ(count, 2);

    MessageCenter::instance().unsubscribe("bulk");
    publish_message_sync("bulk", 1);
    EXPECT_EQ(count, 2); // Unchanged
}

TEST_F(MessageCenterTest, CallableSupport) {
    int val_lambda = 0;
    int val_functor = 0;
    int val_member = 0;
    g_freeFuncVal = 0;

    // 1. Lambda
    auto t1 = subscribe_message<int>("topic_lambda", [&](int i){ val_lambda = i; });
    publish_message_sync("topic_lambda", 10);
    EXPECT_EQ(val_lambda, 10);
    unsubscribe_message("topic_lambda", t1);

    // 2. Functor (Struct with operator())
    auto t2 = subscribe_message<int>("topic_functor", MyFunctor{&val_functor});
    publish_message_sync("topic_functor", 20);
    EXPECT_EQ(val_functor, 20);
    unsubscribe_message("topic_functor", t2);

    // 3. Class Member Function (via std::bind)
    MemberMethod obj{val_member};
    // Bind creates a callable that matches void(int) signature
    auto t3 = subscribe_message<int>("topic_member", std::bind(&MemberMethod::memberFunc, &obj, std::placeholders::_1));
    publish_message_sync("topic_member", 30);
    EXPECT_EQ(val_member, 30);
    unsubscribe_message("topic_member", t3);

    // 4. Free Function Pointer
    auto t4 = subscribe_message<int>("topic_static", &freeFunc);
    publish_message_sync("topic_static", 40);
    EXPECT_EQ(g_freeFuncVal, 40);
    unsubscribe_message("topic_static", t4);
}
