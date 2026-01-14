# EventSystem Project Restoration Context

**Project Status**: Implemented `EventCenter` (Type-based) and `MessageCenter` (String-based) with support for both Synchronous and Asynchronous dispatching. Added convenient Tool-level APIs (`subscribe_event`, `subscribe_message`) for both systems. README updated.

**Instructions for Gemini**: Please parse the following file contents to restore the project context.

## 1. include/EventSystem.h

```cpp
#pragma once

#include <iostream>
#include <vector>
#include <map>
#include <functional>
#include <any>
#include <typeindex>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <chrono>
#include <utility>

using SubscriptionHandle = size_t;

class IEventHandler
{
public:
    virtual ~IEventHandler() = default;
    virtual void handle(const std::any &eventData) = 0;
};

class EventRegistry
{
public:
    virtual ~EventRegistry() = default;

    template <typename TEvent>
    void registerHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].strongRefs.push_back(handler);
    }

    template <typename TEvent>
    void registerWeakHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].weakRefs.push_back(handler);
    }

    template <typename TEvent>
    void unregisterHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        auto it = m_interfaceHandlers.find(eventType);
        if (it != m_interfaceHandlers.end())
        {
            auto &handlerGroup = it->second;
            handlerGroup.strongRefs.erase(
                std::remove(handlerGroup.strongRefs.begin(), handlerGroup.strongRefs.end(), handler),
                handlerGroup.strongRefs.end());

            handlerGroup.weakRefs.erase(
                std::remove_if(handlerGroup.weakRefs.begin(), handlerGroup.weakRefs.end(),
                               [&handler](const std::weak_ptr<IEventHandler> &weak)
                               {
                                   return weak.expired() || weak.lock() == handler;
                               }),
                handlerGroup.weakRefs.end());
        }
    }

    template <typename TEvent>
    SubscriptionHandle registerHandler(std::function<void(const TEvent &)> callback)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        SubscriptionHandle handle = m_nextSubscriptionId++;
        auto wrapper = [callback](const std::any &eventData)
        {
            if (auto *event = std::any_cast<TEvent>(&eventData))
            {
                callback(*event);
            }
        };

        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_callbackHandlers[eventType][handle] = wrapper;
        m_handleToEventTypeMap.emplace(handle, eventType);
        return handle;
    }

    void unregisterHandler(SubscriptionHandle handle)
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        auto it = m_handleToEventTypeMap.find(handle);
        if (it != m_handleToEventTypeMap.end())
        {
            std::type_index eventType = it->second;
            m_callbackHandlers[eventType].erase(handle);
            m_handleToEventTypeMap.erase(it);
        }
    }

    template <typename TEvent>
    void unregisterAllHandlers()
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        auto it_cb = m_callbackHandlers.find(eventType);
        if (it_cb != m_callbackHandlers.end())
        {
            for (const auto &[handle, func] : it_cb->second)
            {
                m_handleToEventTypeMap.erase(handle);
            }
            m_callbackHandlers.erase(it_cb);
        }
        m_interfaceHandlers.erase(eventType);
    }

protected:
    void dispatchEvent(const std::any &eventData, const std::type_index &eventType)
    {
        std::vector<std::shared_ptr<IEventHandler>> strong_handlers;
        std::vector<std::weak_ptr<IEventHandler>> weak_handlers;
        std::vector<GenericCallback> callbacks;

        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            auto it_ih = m_interfaceHandlers.find(eventType);
            if (it_ih != m_interfaceHandlers.end())
            {
                strong_handlers = it_ih->second.strongRefs;
                weak_handlers = it_ih->second.weakRefs;
            }
            auto it_cb = m_callbackHandlers.find(eventType);
            if (it_cb != m_callbackHandlers.end())
            {
                for (const auto &pair : it_cb->second)
                {
                    callbacks.push_back(pair.second);
                }
            }
        }

        auto safeInvoke = [&](const auto &action, const char *typeLabel)
        {
            try
            {
                auto start = std::chrono::steady_clock::now();
                action();
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if (duration.count() > 500)
                {
                    std::cerr << "[EventSystem] Warning: " << typeLabel << " took " << duration.count() << "ms" << std::endl;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "[EventSystem] Exception: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "[EventSystem] Unknown exception." << std::endl;
            }
        };

        for (const auto &handler : strong_handlers) safeInvoke([&]() { handler->handle(eventData); }, "StrongHandler");
        for (const auto &weak_handler : weak_handlers) if (auto handler = weak_handler.lock()) safeInvoke([&]() { handler->handle(eventData); }, "WeakHandler");
        for (const auto &callback : callbacks) safeInvoke([&]() { callback(eventData); }, "CallbackHandler");
    }

    struct InterfaceHandlers { std::vector<std::shared_ptr<IEventHandler>> strongRefs; std::vector<std::weak_ptr<IEventHandler>> weakRefs; };
    using GenericCallback = std::function<void(const std::any &)>;
    std::map<std::type_index, InterfaceHandlers> m_interfaceHandlers;
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> m_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> m_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> m_nextSubscriptionId{0};
    std::mutex m_registryMutex;
};

class SyncEventCenter : public EventRegistry
{
public:
    static SyncEventCenter &instance() { /* Singleton implementation */ }
    template <typename TEvent> void publish_event(const TEvent &event) { dispatchEvent(event, std::type_index(typeid(TEvent))); }
};

class AsyncEventCenter : public EventRegistry
{
public:
    static AsyncEventCenter &instance() { /* Singleton implementation */ }
    template <typename TEvent> void publish_event(const TEvent &event) { publish_event_at(event, std::chrono::steady_clock::now()); }
    template <typename TEvent> void publish_event_delayed(const TEvent &event, std::chrono::milliseconds delay) { publish_event_at(event, std::chrono::steady_clock::now() + delay); }
    template <typename TEvent> void publish_event_at(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint) { /* Implementation */ }
    void cancelAllEvents() { /* Implementation */ }
};

using EventCenter = AsyncEventCenter;

// Helper Tools (Publishing)
template <typename TEvent> void publish_event_sync(const TEvent &event) { SyncEventCenter::instance().publish_event(event); }
template <typename TEvent> void publish_event_async(const TEvent &event) { AsyncEventCenter::instance().publish_event(event); }
template <typename TEvent> void publish_event(const TEvent &event) { publish_event_async(event); }
inline void cancelAllEvents() { EventCenter::instance().cancelAllEvents(); }

// Helper Tools (Convenient Subscribing)
template <typename TEvent> SubscriptionHandle subscribe_event_async(std::function<void(const TEvent&)> callback) { return AsyncEventCenter::instance().registerHandler<TEvent>(std::move(callback)); }
template <typename TEvent> SubscriptionHandle subscribe_event_sync(std::function<void(const TEvent&)> callback) { return SyncEventCenter::instance().registerHandler<TEvent>(std::move(callback)); }
template <typename TEvent> SubscriptionHandle subscribe_event(std::function<void(const TEvent&)> callback) { return subscribe_event_async<TEvent>(std::move(callback)); }
inline void unsubscribe_event_async(SubscriptionHandle handle) { AsyncEventCenter::instance().unregisterHandler(handle); }
inline void unsubscribe_event_sync(SubscriptionHandle handle) { SyncEventCenter::instance().unregisterHandler(handle); }
inline void unsubscribe_event(SubscriptionHandle handle) { unsubscribe_event_async(handle); }

template <typename TEvent> SubscriptionHandle registerStaticEventHandler() { return EventCenter::instance().registerHandler<TEvent>(&TEvent::handle); }
```

## 2. include/MessageCenter.h

```cpp
#pragma once
/* String-based MessageCenter implementation with Sync/Async support and Tool-level helpers */
/* subscribe_message, publish_message, etc. */
```

## 3. README.md

```markdown
# C++17 Asynchronous Event System (NotifyCenter)
Supports Type-based (EventCenter) and String-based (MessageCenter).
Features: Sync/Async, Exception Isolation, Performance Monitoring, Memory Safety.
Usage: subscribe_event<T>(callback), publish_event(event), subscribe_message(topic, callback), publish_message(topic, message).
```

## 4. app/main.cpp (Partial - Step 12 Demo)

```cpp
    // STEP 12: New Convenient Event Subscription API Demo
    auto conv_token = subscribe_event<ConvenientEvent>([](const ConvenientEvent& e) {
        std::cout << "    -> [Convenient-Async] Received: " << e.msg << std::endl;
    });
    publish_event(ConvenientEvent{"Testing subscribe_event"});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    unsubscribe_event(conv_token);
```
