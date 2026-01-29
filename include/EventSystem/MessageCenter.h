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

class EVENTSYSTEM_API MessageCenter {
public:
    using SubscriptionToken = size_t;

    static MessageCenter& Instance();

    MessageCenter(const MessageCenter&) = delete;
    MessageCenter& operator=(const MessageCenter&) = delete;

    ~MessageCenter();

    /**
     * @brief Set the default publish mode (Async or Sync).
     * Default is Async.
     */
    void SetPublishMode(PublishMode mode);

    /**
     * @brief Get the current publish mode.
     */
    PublishMode GetPublishMode() const;

    template <typename... Args>
    SubscriptionToken Subscribe(const std::string& topic, std::function<void(Args...)> callback) {
        return SubscribeInternal(topic, std::any(callback));
    }

    void Unsubscribe(const std::string& topic, SubscriptionToken token);
    void Unsubscribe(const std::string& topic);

    void PrintSubscriptions();

    template <typename... Args>
    void PublishSync(const std::string& topic, Args&&... args) {
        Dispatch<typename std::decay<Args>::type...>(topic, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void PublishAsync(const std::string& topic, Args&&... args) {
        auto task = [this, topic, args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            std::apply([this, &topic](auto&&... unpackedArgs) {
                this->Dispatch<typename std::decay<Args>::type...>(topic, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
            }, std::move(args));
        };
        EnqueueTask(std::move(task));
    }

    template <typename... Args>
    void Publish(const std::string& topic, Args&&... args) {
        if (GetPublishMode() == PublishMode::Sync) {
            PublishSync(topic, std::forward<Args>(args)...);
        } else {
            PublishAsync(topic, std::forward<Args>(args)...);
        }
    }

private:
    MessageCenter();
    
    SubscriptionToken SubscribeInternal(const std::string& topic, std::any callback);
    void EnqueueTask(std::function<void()> task);
    std::vector<std::any> GetCallbacksInternal(const std::string& topic);

    template <typename... Args>
    void Dispatch(const std::string& topic, Args... args) {
        std::vector<std::any> callbacksToInvoke = GetCallbacksInternal(topic);
        for (const auto& anyCb : callbacksToInvoke) {
            try {
                using FunctionType = std::function<void(Args...)>;
                if (auto* func = std::any_cast<FunctionType>(&anyCb)) {
                    (*func)(args...);
                }
            } catch (const std::exception& e) {
                std::cerr << "[MessageCenter] Exception during dispatch for '" << topic << "': " << e.what() << std::endl;
            }
        }
    }

    struct Impl;
    Impl* m_impl;
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

    template <typename Callback, typename... Args>
    MessageCenter::SubscriptionToken SubscribeHelper(const std::string& topic, Callback&& cb, std::tuple<Args...>*) {
        return MessageCenter::Instance().Subscribe<Args...>(topic, std::function<void(Args...)>(std::forward<Callback>(cb)));
    }
}

template <typename... Args, typename Callback>
MessageCenter::SubscriptionToken SubscribeMessage(const std::string& topic, Callback&& callback) {
    if constexpr (sizeof...(Args) > 0) {
        return MessageCenter::Instance().Subscribe<Args...>(topic, std::function<void(Args...)>(std::forward<Callback>(callback)));
    } else {
        using Traits = detail::function_traits<std::decay_t<Callback>>;
        using ArgsTuple = typename Traits::args_tuple;
        return detail::SubscribeHelper(topic, std::forward<Callback>(callback), (ArgsTuple*)nullptr);
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
void PublishMessageSync(const std::string& topic, Args&&... args) {
    MessageCenter::Instance().PublishSync(topic, std::forward<Args>(args)...);
}

inline void UnsubscribeMessage(const std::string& topic, MessageCenter::SubscriptionToken token) {
    MessageCenter::Instance().Unsubscribe(topic, token);
}

inline void UnsubscribeMessage(const std::string& topic) {
    MessageCenter::Instance().Unsubscribe(topic);
}

inline void SetMessageCenterPublishMode(eventsystem::PublishMode mode) {
    MessageCenter::Instance().SetPublishMode(mode);
}

inline eventsystem::PublishMode GetMessageCenterPublishMode() {
    return MessageCenter::Instance().GetPublishMode();
}

} // namespace eventsystem
