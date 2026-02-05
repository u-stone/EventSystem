#pragma once

#include "Export.h"
#include <string>
#include <functional>
#include <memory>
#include <any>
#include <vector>
#include <iostream>
#include <typeindex>
#include <tuple>
#include <chrono>

namespace eventsystem {

namespace detail {
    // 1. Promote const char* / char* to std::string
    inline std::string promote(const char* s) { return std::string(s); }
    inline std::string promote(char* s) { return std::string(s); }
    
    // 2. Pass other types as-is
    template <typename T>
    decltype(auto) promote(T&& arg) { return std::forward<T>(arg); }
    
    // 3. Helper to get the promoted type
    template <typename T>
    using promoted_type = std::decay_t<decltype(promote(std::declval<T>()))>;
}

class EVENTSYSTEM_API MessageCenter {
public:
    using SubscriptionToken = size_t;

    static MessageCenter& Instance();

    /**
     * @brief Explicitly destroys the singleton instance.
     * Important for avoiding deadlocks on Windows DLL unload if the worker thread is running.
     * Should be called before main() exits.
     */
    static void Destroy();

    MessageCenter(const MessageCenter&) = delete;
    MessageCenter& operator=(const MessageCenter&) = delete;

    ~MessageCenter();

    /**
     * @brief Set the default publish mode (Async, Sync, or MainThread).
     * Default is MainThread (Queued for Update() in the main game loop).
     */
    void SetPublishMode(PublishMode mode);

    /**
     * @brief Get the current publish mode.
     */
    PublishMode GetPublishMode() const;

    /**
     * @brief Process pending messages in the MainThread queue.
     * Should be called once per frame in the main game loop.
     */
    void Update();

    /**
     * @brief Set the maximum time (in milliseconds) the Update() method is allowed to run.
     * If 0, it processes all pending messages.
     * @param ms Maximum duration in milliseconds.
     */
    void SetMaxUpdateDuration(double ms);

    template <typename... Args>
    SubscriptionToken Subscribe(const std::string& topic, std::function<void(Args...)> callback) {
        return SubscribeInternal(topic, std::any(callback));
    }

    void Unsubscribe(const std::string& topic, SubscriptionToken token);
    void Unsubscribe(const std::string& topic);

    void PrintSubscriptions();

    template <typename... Args>
    void PublishSync(const std::string& topic, Args&&... args) {
        Dispatch<detail::promoted_type<Args>...>(topic, detail::promote(std::forward<Args>(args))...);
    }

    template <typename... Args>
    void PublishAsync(const std::string& topic, Args&&... args) {
        auto task = [this, topic, 
                     args = std::make_tuple(detail::promote(std::forward<Args>(args))...)]() mutable {
            std::apply([this, &topic](auto&&... unpackedArgs) {
                this->Dispatch<typename std::decay<decltype(unpackedArgs)>::type...>(
                    topic, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
            }, std::move(args));
        };
        EnqueueTask(std::move(task));
    }

    template <typename... Args>
    void PublishMainThread(const std::string& topic, Args&&... args) {
        auto task = [this, topic, 
                     args = std::make_tuple(detail::promote(std::forward<Args>(args))...)]() mutable {
            std::apply([this, &topic](auto&&... unpackedArgs) {
                this->Dispatch<typename std::decay<decltype(unpackedArgs)>::type...>(
                    topic, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
            }, std::move(args));
        };
        EnqueueMainThreadTask(std::move(task));
    }

    template <typename... Args>
    void Publish(const std::string& topic, Args&&... args) {
        PublishMode mode = GetPublishMode();
        if (mode == PublishMode::Sync) {
            PublishSync(topic, std::forward<Args>(args)...);
        } else if (mode == PublishMode::MainThread) {
            PublishMainThread(topic, std::forward<Args>(args)...);
        } else {
            PublishAsync(topic, std::forward<Args>(args)...);
        }
    }

private:
    MessageCenter();
    
    SubscriptionToken SubscribeInternal(const std::string& topic, std::any callback);
    void EnqueueTask(std::function<void()> task);
    void EnqueueMainThreadTask(std::function<void()> task);
    std::vector<std::any> GetCallbacksInternal(const std::string& topic);

    template <typename... Args>
    void Dispatch(const std::string& topic, Args... args) {
        std::vector<std::any> callbacksToInvoke = GetCallbacksInternal(topic);
        for (const auto& anyCb : callbacksToInvoke) {
            using FunctionType = std::function<void(Args...)>;
            if (auto* func = std::any_cast<FunctionType>(&anyCb)) {
                try {
                    (*func)(args...);
                } catch (const std::exception& e) {
                    std::cerr << "[MessageCenter] Exception during dispatch for '" 
                              << topic << "': " << e.what() << std::endl;
                }
            } else {
                // Type mismatch logging to help users identify why their message was ignored.
                std::cerr << "[MessageCenter] Mismatch function prototype for topic '" << topic << "':\n"
                          << "  Publisher provided: " << typeid(FunctionType).name() << "\n"
                          << "  Subscriber expects: " << anyCb.type().name() << std::endl;
            }
        }
    }

    struct Impl;
    Impl* m_impl;
};

/**
 * @brief RAII wrapper for automatic unsubscription.
 * Useful for managing subscription lifetime in dynamic modules (DLLs) to prevent dangling callbacks.
 */
class ScopedSubscription {
public:
    ScopedSubscription() = default;

    ScopedSubscription(const std::string& topic, MessageCenter::SubscriptionToken token)
        : m_topic(topic), m_token(token), m_valid(true) {}

    ~ScopedSubscription() {
        Reset();
    }

    ScopedSubscription(ScopedSubscription&& other) noexcept {
        MoveFrom(std::move(other));
    }

    ScopedSubscription& operator=(ScopedSubscription&& other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;

    void Reset() {
        if (m_valid) {
            MessageCenter::Instance().Unsubscribe(m_topic, m_token);
            m_valid = false;
        }
    }

private:
    void MoveFrom(ScopedSubscription&& other) {
        m_topic = std::move(other.m_topic);
        m_token = other.m_token;
        m_valid = other.m_valid;
        other.m_valid = false;
    }

    std::string m_topic;
    MessageCenter::SubscriptionToken m_token = 0;
    bool m_valid = false;
};

// ... Helpers ...
namespace detail {
    template <typename T> struct function_traits : public function_traits<decltype(&T::operator())> {};

    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType(ClassType::*)(Args...) const> {
        using args_tuple = std::tuple<Args...>;
    };

    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType(ClassType::*)(Args...)> {
        using args_tuple = std::tuple<Args...>;
    };

    // Helper that decays argument types to ensure signature consistency (e.g. const string& -> string)
    template <typename Callback, typename... Args>
    MessageCenter::SubscriptionToken SubscribeHelperDecayed(const std::string& topic, 
                                                            Callback&& cb, 
                                                            std::tuple<Args...>*) {
        return MessageCenter::Instance().Subscribe<std::decay_t<Args>...>(
            topic, 
            std::function<void(std::decay_t<Args>...)>(std::forward<Callback>(cb))
        );
    }
}

template <typename... Args, typename Callback>
MessageCenter::SubscriptionToken SubscribeMessage(const std::string& topic, Callback&& callback) {
    if constexpr (sizeof...(Args) > 0) {
        return MessageCenter::Instance().Subscribe<Args...>(
            topic, 
            std::function<void(Args...)>(std::forward<Callback>(callback))
        );
    } else {
        using Traits = detail::function_traits<std::decay_t<Callback>>;
        using ArgsTuple = typename Traits::args_tuple;
        // Use the Decayed helper to normalize types (e.g. const std::string& -> std::string)
        return detail::SubscribeHelperDecayed(topic, std::forward<Callback>(callback), 
                                              (ArgsTuple*)nullptr);
    }
}

template <typename... Args>
void PublishMessage(const std::string& topic, Args&&... args) {
    MessageCenter::Instance().Publish(topic, std::forward<Args>(args)...);
}

template <typename... Args>
void PublishMessageAsync(const std::string& topic, Args&&... args) {
    MessageCenter::Instance().PublishAsync(topic, std::forward<Args>(args)...);
}

template <typename... Args>
void PublishMessageMainThread(const std::string& topic, Args&&... args) {
    MessageCenter::Instance().PublishMainThread(topic, std::forward<Args>(args)...);
}

template <typename... Args>
void PublishMessageSync(const std::string& topic, Args&&... args) {
    MessageCenter::Instance().PublishSync(topic, std::forward<Args>(args)...);
}

inline void UnsubscribeMessage(const std::string& topic, MessageCenter::SubscriptionToken token) {
    MessageCenter::Instance().Unsubscribe(topic, token);
}

inline void UnsubscribeMessage(const std::string& topic) {
    MessageCenter::Instance().Unsubscribe(topic);
}

inline void UpdateMessageCenter() {
    MessageCenter::Instance().Update();
}

inline void SetMessageCenterMaxUpdateDuration(double ms) {
    MessageCenter::Instance().SetMaxUpdateDuration(ms);
}

inline void SetMessageCenterPublishMode(PublishMode mode) {
    MessageCenter::Instance().SetPublishMode(mode);
}

inline PublishMode GetMessageCenterPublishMode() {
    return MessageCenter::Instance().GetPublishMode();
}

inline void PrintMessageSubscriptions() {
    MessageCenter::Instance().PrintSubscriptions();
}

inline void DestroyMessageCenter() {
    MessageCenter::Destroy();
}

} // namespace eventsystem
