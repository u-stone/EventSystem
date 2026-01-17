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
        return subscribeImpl(topic, std::any(callback));
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

        queueTask(std::move(task));
    }

    // Default publish is Async
    template <typename... Args>
    void publish(const std::string& topic, Args&&... args) {
        publishAsync(topic, std::forward<Args>(args)...);
    }

private:
    MessageCenter();
    
    // Internal generic dispatch logic
    template <typename... Args>
    void dispatch(const std::string& topic, Args... args) {
        // Get callbacks via Pimpl helper
        auto callbacksToInvoke = getSubscribers(topic);

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

    struct Impl;
#pragma warning(push)
#pragma warning(disable: 4251)
    std::unique_ptr<Impl> m_impl;
#pragma warning(pop)

    // Helper methods for Pimpl
    SubscriptionToken subscribeImpl(const std::string& topic, std::any callback);
    std::vector<std::any> getSubscribers(const std::string& topic);
    void queueTask(std::function<void()> task);
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