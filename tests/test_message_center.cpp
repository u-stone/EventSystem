#include <gtest/gtest.h>
#include "EventSystem/MessageCenter.h"
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional> 
#include <future>

using namespace eventsystem;

class MessageCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        DestroyMessageCenter();
    }
    void TearDown() override {
        DestroyMessageCenter();
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

    UnsubscribeMessage("bulk");
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
    auto token = SubscribeMessage<int>("member_bind", 
                                       std::bind(&MemberMethodTester::OnMessage, &tester, _1));
    
    PublishMessageSync("member_bind", 456);
    EXPECT_EQ(tester.val, 456);
    UnsubscribeMessage("member_bind", token);
}

// --- String Literal Promotion Test ---

TEST_F(MessageCenterTest, PublishStringLiteral) {
    std::string received;
    auto token = SubscribeMessage<std::string>("literal_test", [&](const std::string& msg) {
        received = msg;
    });

    // Pass const char*, expecting auto-promotion to std::string
    PublishMessageSync("literal_test", "hello world");
    
    EXPECT_EQ(received, "hello world");
    UnsubscribeMessage("literal_test", token);
}

TEST_F(MessageCenterTest, ImplicitRefToStringMatch) {
    std::string received;
    // Subscriber uses const std::string& (implicit)
    // Should be decayed to std::string for signature matching
    auto token = SubscribeMessage("implicit_ref_test", [&](const std::string& s) {
        received = s;
    });

    // Publisher uses std::string
    std::string msg = "works";
    PublishMessageSync("implicit_ref_test", msg);

    EXPECT_EQ(received, "works");
    UnsubscribeMessage("implicit_ref_test", token);
}

// --- Additional Tests for Safety & Reference Wrapper ---

TEST_F(MessageCenterTest, AsyncLifetimeSafety) {
    std::atomic<int> received_val{0};
    auto token = SubscribeMessage("async_lifetime", [&](const std::vector<int>& vec) {
        // If the vector was a dangling reference, accessing it would crash or give garbage.
        // We expect a valid copy.
        if (!vec.empty()) {
            received_val = vec[0];
        }
    });

    {
        std::vector<int> temp_vec = { 999 };
        // Publish async. temp_vec will be destroyed immediately after this block.
        // The system MUST copy it.
        PublishMessageAsync("async_lifetime", temp_vec);
    } // temp_vec destroyed here

    // Give time for worker to process
    int retries = 0;
    while(received_val == 0 && retries++ < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_EQ(received_val, 999);
    UnsubscribeMessage("async_lifetime", token);
}

TEST_F(MessageCenterTest, ReferenceWrapperModification) {
    int target = 10;
    // To modify 'target', we must explicitly subscribe to reference_wrapper<int>
    
    auto token = SubscribeMessage<std::reference_wrapper<int>>("ref_wrapper_test", 
        [](std::reference_wrapper<int> val) {
            val.get() = 20;
        });

    PublishMessageSync("ref_wrapper_test", std::ref(target));
    
    EXPECT_EQ(target, 20);
    UnsubscribeMessage("ref_wrapper_test", token);
}

TEST_F(MessageCenterTest, ImplicitDeductionStringLiteral) {
    std::string received;
    // Implicit deduction from lambda taking const std::string&
    auto token = SubscribeMessage("implicit_string_literal", [&](const std::string& s) {
        received = s;
    });

    // Publish const char*
    PublishMessageSync("implicit_string_literal", "hello implicit");
    
    EXPECT_EQ(received, "hello implicit");
    UnsubscribeMessage("implicit_string_literal", token);
}

TEST_F(MessageCenterTest, ScopedSubscriptionRAII) {
    int call_count = 0;
    {
        // Scope block
        auto token = SubscribeMessage<>("scoped_test", [&](){ call_count++; });
        ScopedSubscription scope("scoped_test", token);
        
        PublishMessageSync("scoped_test");
        EXPECT_EQ(call_count, 1);
        // scope destroys here -> Unsubscribe
    }

    PublishMessageSync("scoped_test");
    EXPECT_EQ(call_count, 1); // Should not increase
}

TEST_F(MessageCenterTest, MainThreadUpdate) {
    bool executed = false;
    SubscribeMessage<int>("MainThreadTopic", [&](int val) {
        executed = true;
        EXPECT_EQ(val, 42);
    });

    // Explicitly publish to MainThread queue
    PublishMessageMainThread("MainThreadTopic", 42);

    // Should not have executed yet
    EXPECT_FALSE(executed);

    // Run update to process queue
    UpdateMessageCenter();

    EXPECT_TRUE(executed);
}

TEST_F(MessageCenterTest, DefaultPublishModeIsMainThread) {
    // New default is MainThread
    // Reset publish mode to default (MainThread)
    SetMessageCenterPublishMode(eventsystem::PublishMode::MainThread);
    
    bool executed = false;
    SubscribeMessage<>("DefaultTopic", [&](){ executed = true; });

    PublishMessage("DefaultTopic"); // Should go to MainThread queue
    EXPECT_FALSE(executed);

    UpdateMessageCenter();
    EXPECT_TRUE(executed);
}

TEST_F(MessageCenterTest, TimeSlicing) {
    std::atomic<int> executionCount{0};
    SubscribeMessage<>("HeavyTopic", [&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        executionCount++;
    });

    // Queue 5 tasks, each taking ~10ms
    for(int i=0; i<5; ++i) {
        PublishMessageMainThread("HeavyTopic");
    }

    // Allow only 25ms (enough for 2 tasks. 10ms + 10ms = 20ms < 25ms. 3rd would make it > 25ms but check is AFTER execution)
    // Execution 1: 10ms. elapsed 10 < 25.
    // Execution 2: 20ms. elapsed 20 < 25.
    // Execution 3: 30ms. elapsed 30 >= 25 -> STOP.
    SetMessageCenterMaxUpdateDuration(25.0); 
    UpdateMessageCenter();

    EXPECT_GE(executionCount, 2);
    EXPECT_LE(executionCount, 3); 

    // Process remaining
    SetMessageCenterMaxUpdateDuration(0); // Unlimited
    UpdateMessageCenter();
    EXPECT_EQ(executionCount, 5);
}

TEST_F(MessageCenterTest, MixedModes) {
    std::promise<void> asyncPromise;
    auto future = asyncPromise.get_future();
    bool mainExecuted = false;

    SubscribeMessage<std::string>("MixedTopic", [&](std::string source){
        if (source == "Async") asyncPromise.set_value();
        if (source == "Main") mainExecuted = true;
    });

    PublishMessageAsync("MixedTopic", std::string("Async"));
    PublishMessageMainThread("MixedTopic", std::string("Main"));

    // Async should finish independently
    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    
    // Main shouldn't have run yet
    EXPECT_FALSE(mainExecuted);

    UpdateMessageCenter();
    EXPECT_TRUE(mainExecuted);
}
