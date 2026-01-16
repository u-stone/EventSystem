#pragma once

#include "Export.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <any>
#include <iostream>
#include <typeindex>

namespace eventsystem {

/**
 * @brief MessageCenter: A robust, thread-safe, string-topic based event system.
 * Supports synchronous and asynchronous dispatch with arbitrary argument lists.
 */
class EVENTSYSTEM_API MessageCenter {
public:
    using SubscriptionToken = size_t;

    static MessageCenter& instance();

    MessageCenter(const MessageCenter&) = delete;
    MessageCenter& operator=(const MessageCenter&) = delete;

    ~MessageCenter();

    /**
     * @brief Subscribe to a topic with a specific function signature.
     * Usage: subscribe<int, std::string>("topic", [](int a, std::string b){ ... });
     */
    template <typename... Args>
    SubscriptionToken subscribe(const std::string& topic, std::function<void(Args...)> callback) {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        SubscriptionToken token = m_nextToken++;
        // We store the std::function<void(Args...)> inside std::any
        m_subscriptions[topic].push_back({token, std::any(callback)});
        return token;
    }

    /**
     * @brief Subscribe overload for simple lambdas where types are explicit in the lambda? 
     * C++17 deduction from lambda is tricky. We encourage using the template version or std::function.
     */

    void unsubscribe(const std::string& topic, SubscriptionToken token);
    void unsubscribe(const std::string& topic);

    /**
     * @brief Publish a message synchronously.
     * The subscribers must match the exact signature of arguments provided.
     */
    template <typename... Args>
    void publishSync(const std::string& topic, Args&&... args) {
        dispatch<typename std::decay<Args>::type...>(topic, std::forward<Args>(args)...);
    }

    /**
     * @brief Publish a message asynchronously.
     * Arguments are copied/moved into the queue.
     */
    template <typename... Args>
    void publishAsync(const std::string& topic, Args&&... args) {
        // Capture arguments by value (decayed) to ensure they survive until execution
        auto task = [this, topic, args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            std::apply([this, &topic](auto&&... unpackedArgs) {
                this->dispatch<typename std::decay<Args>::type...>(topic, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
            }, std::move(args));
        };

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.emplace(std::move(task));
        }
        m_cv.notify_one();
    }

    // Default publish is Async
    template <typename... Args>
    void publish(const std::string& topic, Args&&... args) {
        publishAsync(topic, std::forward<Args>(args)...);
    }

private:
    MessageCenter();
    void stop();
    void workerLoop();

    // Internal generic dispatch logic
    template <typename... Args>
    void dispatch(const std::string& topic, Args... args) {
        // Copy callbacks to avoid holding lock during execution
        std::vector<std::any> callbacksToInvoke;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            auto it = m_subscriptions.find(topic);
            if (it != m_subscriptions.end()) {
                for (const auto& entry : it->second) {
                    callbacksToInvoke.push_back(entry.callback);
                }
            }
        }

        // Execute
        for (const auto& anyCb : callbacksToInvoke) {
            try {
                // Try to cast to the exact function signature
                using FunctionType = std::function<void(Args...)>;
                // Note: std::any_cast throws bad_any_cast if type doesn't match exactly.
                // We want to skip mismatches silently (or log debug), not crash.
                // However, any_cast on pointer returns nullptr if mismatch.
                if (auto* func = std::any_cast<FunctionType>(&anyCb)) {
                    (*func)(args...);
                }
            } catch (const std::exception& e) {
                std::cerr << "[MessageCenter] Exception during dispatch for '" << topic << "': " << e.what() << std::endl;
            }
        }
    }

    struct SubscriberEntry {
        SubscriptionToken id;
        std::any callback; // Stores std::function<void(Args...)>
    };

    std::unordered_map<std::string, std::vector<SubscriberEntry>> m_subscriptions;
    std::mutex m_registryMutex;
    std::atomic<SubscriptionToken> m_nextToken{0};

    // Async Queue holds generic void() functors (closures)
    std::queue<std::function<void()>> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running;
};

//----------------------------------------------------------------
// Helper "Tool" functions
//----------------------------------------------------------------

// Unified Subscribe (Template)
// Usage: subscribe_message<int>("topic", [](int i){...})
template <typename... Args, typename Callback>
MessageCenter::SubscriptionToken subscribe_message(const std::string& topic, Callback&& callback) {
    return MessageCenter::instance().subscribe<Args...>(topic, std::function<void(Args...)>(std::forward<Callback>(callback)));
}

// Explicit overload for std::string (Legacy support for simple lambdas without template args)
inline MessageCenter::SubscriptionToken subscribe_message(const std::string& topic, std::function<void(const std::string&)> callback) {
    return MessageCenter::instance().subscribe<std::string>(topic, std::move(callback));
}

// Publish wrappers
template <typename... Args>
void publish_message(const std::string& topic, Args&&... args) {
    MessageCenter::instance().publishAsync(topic, std::forward<Args>(args)...);
}

template <typename... Args>
void publish_message_async(const std::string& topic, Args&&... args) {
    MessageCenter::instance().publishAsync(topic, std::forward<Args>(args)...);
}

template <typename... Args>
void publish_message_sync(const std::string& topic, Args&&... args) {
    MessageCenter::instance().publishSync(topic, std::forward<Args>(args)...);
}

inline void unsubscribe_message(const std::string& topic, MessageCenter::SubscriptionToken token) {
    MessageCenter::instance().unsubscribe(topic, token);
}

inline void unsubscribe_message(const std::string& topic) {
    MessageCenter::instance().unsubscribe(topic);
}

} // namespace eventsystem