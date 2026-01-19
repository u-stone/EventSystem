#include <gtest/gtest.h>
#include "EventSystem/MessageCenter.h"
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional> 

using namespace eventsystem;

class MessageCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No explicit reset needed as we use different topics usually
    }
};

// --- Helpers for Callable Tests ---

void FreeFunction(int i, int& out) {
    out = i * 2;
}

struct Functor {
    int& out;
    void operator()(int i) const {
        out = i * 3;
    }
};

class MemberMethodTester {
public:
    int val = 0;
    void OnMessage(int i) {
        val = i;
    }
};

// --- Tests ---

TEST_F(MessageCenterTest, BasicString) {
    std::string result;
    auto token = SubscribeMessage<std::string>("test_str", [&](const std::string& s) {
        result = s;
    });

    PublishMessageSync("test_str", std::string("Hello"));
    EXPECT_EQ(result, "Hello");
    UnsubscribeMessage("test_str", token);
}

TEST_F(MessageCenterTest, ZeroArgs) {
    bool called = false;
    auto token = SubscribeMessage<>("test_void", [&]() {
        called = true;
    });

    PublishMessageSync("test_void");
    EXPECT_TRUE(called);
    UnsubscribeMessage("test_void", token);
}

TEST_F(MessageCenterTest, MultiArgs) {
    int v1 = 0;
    float v2 = 0.0f;
    std::string v3;

    auto token = SubscribeMessage<int, float, std::string>("test_multi", 
        [&](int i, float f, std::string s) {
            v1 = i; v2 = f; v3 = s;
        });

    PublishMessageSync("test_multi", 42, 3.14f, std::string("Pi"));
    
    EXPECT_EQ(v1, 42);
    EXPECT_FLOAT_EQ(v2, 3.14f);
    EXPECT_EQ(v3, "Pi");

    UnsubscribeMessage("test_multi", token);
}

TEST_F(MessageCenterTest, TypeMismatchIgnored) {
    bool called = false;
    auto token = SubscribeMessage<int>("test_mismatch", [&](int) {
        called = true;
    });

    // Publish float, subscriber expects int. Should NOT call.
    PublishMessageSync("test_mismatch", 3.14f);
    EXPECT_FALSE(called);

    // Publish int
    PublishMessageSync("test_mismatch", 100);
    EXPECT_TRUE(called);

    UnsubscribeMessage("test_mismatch", token);
}

TEST_F(MessageCenterTest, AsyncMultiArgs) {
    std::atomic<int> sum{0};
    auto token = SubscribeMessage<int, int>("test_async_add", [&](int a, int b) {
        sum = a + b;
    });

    PublishMessageAsync("test_async_add", 10, 20);
    
    int retries = 0;
    while(sum == 0 && retries++ < 10) std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(sum, 30);
    UnsubscribeMessage("test_async_add", token);
}

TEST_F(MessageCenterTest, UnsubscribeAll) {
    int count = 0;
    SubscribeMessage<int>("bulk", [&](int){ count++; });
    SubscribeMessage<int>("bulk", [&](int){ count++; });

    PublishMessageSync("bulk", 1);
    EXPECT_EQ(count, 2);

    eventsystem::UnsubscribeMessage("bulk");
    PublishMessageSync("bulk", 1);
    EXPECT_EQ(count, 2); // Unchanged
}

TEST_F(MessageCenterTest, ImplicitDeduction) {
    bool called = false;
    // No <int> specified! Traits should deduce it.
    auto token = SubscribeMessage("implicit", [&](int v) {
        EXPECT_EQ(v, 999);
        called = true;
    });
    
    PublishMessageSync("implicit", 999);
    EXPECT_TRUE(called);
    UnsubscribeMessage("implicit", token);
}

// --- New Callable Tests ---

TEST_F(MessageCenterTest, FreeFunctionPointer) {
    int result = 0;
    
    // We must use std::reference_wrapper because the system decays arguments for safety.
    auto wrapper_func = [](int i, std::reference_wrapper<int> out) {
        out.get() = i * 2;
    };

    auto token = SubscribeMessage<int, std::reference_wrapper<int>>("free_func", wrapper_func);
    
    // Pass std::ref(result)
    PublishMessageSync("free_func", 10, std::ref(result));
    EXPECT_EQ(result, 20);
    UnsubscribeMessage("free_func", token);
}

TEST_F(MessageCenterTest, FunctorObject) {
    int result = 0;
    Functor f{result};
    
    // Implicit deduction should work for Functor struct because it has operator()
    auto token = SubscribeMessage("functor", f);
    
    PublishMessageSync("functor", 10);
    EXPECT_EQ(result, 30);
    UnsubscribeMessage("functor", token);
}

TEST_F(MessageCenterTest, MemberFunctionLambda) {
    MemberMethodTester tester;
    // Standard way: wrap in lambda capturing this
    auto token = SubscribeMessage("member_lambda", [&](int i){
        tester.OnMessage(i);
    });
    
    PublishMessageSync("member_lambda", 123);
    EXPECT_EQ(tester.val, 123);
    UnsubscribeMessage("member_lambda", token);
}

TEST_F(MessageCenterTest, MemberFunctionBind) {
    MemberMethodTester tester;
    using namespace std::placeholders;
    
    // std::bind returns an unspecified type. 
    auto token = SubscribeMessage<int>("member_bind", std::bind(&MemberMethodTester::OnMessage, &tester, _1));
    
    PublishMessageSync("member_bind", 456);
    EXPECT_EQ(tester.val, 456);
    UnsubscribeMessage("member_bind", token);
}