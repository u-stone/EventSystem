#pragma once

#include "Export.h"
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

namespace eventsystem {

// A unique identifier for a callback subscription, used for unregistering.
using SubscriptionHandle = size_t;

//----------------------------------------------------------------
// Base class for all event handlers.
//----------------------------------------------------------------
class EVENTSYSTEM_API IEventHandler
{
public:
    virtual ~IEventHandler() = default;
    virtual void handle(const std::any &eventData) = 0;
};

//----------------------------------------------------------------
// EventRegistry: Manages shared event subscriptions for all event centers.
//----------------------------------------------------------------
class EVENTSYSTEM_API EventRegistry
{
public:
    virtual ~EventRegistry() = default;

    // --- IEventHandler-based Subscription (Static / Shared) ---

    template <typename TEvent>
    static void registerHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].strongRefs.push_back(handler);
    }

    template <typename TEvent>
    static void registerWeakHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].weakRefs.push_back(handler);
    }

    template <typename TEvent>
    static void unregisterHandler(const std::shared_ptr<IEventHandler> &handler)
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

    // --- Callback-based Subscription (Static / Shared) ---
    template <typename TEvent>
    static SubscriptionHandle registerHandler(std::function<void(const TEvent &)> callback)
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

    static void unregisterHandler(SubscriptionHandle handle);

    template <typename TEvent>
    static void unregisterAllHandlers()
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

    // Clears all subscriptions (for testing or system reset)
    static void reset();

protected:
    static void dispatchEvent(const std::any &eventData, const std::type_index &eventType);

    struct InterfaceHandlers
    {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };

    using GenericCallback = std::function<void(const std::any &)>;

    // Shared State
    static std::map<std::type_index, InterfaceHandlers> m_interfaceHandlers;
    static std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> m_callbackHandlers;
    static std::map<SubscriptionHandle, std::type_index> m_handleToEventTypeMap;
    static std::atomic<SubscriptionHandle> m_nextSubscriptionId;
    static std::mutex m_registryMutex;
};

//----------------------------------------------------------------
// SyncEventCenter
//----------------------------------------------------------------
class EVENTSYSTEM_API SyncEventCenter : public EventRegistry
{
public:
    static SyncEventCenter &instance();
    static void destroy();

    SyncEventCenter(const SyncEventCenter &) = delete;
    SyncEventCenter &operator=(const SyncEventCenter &) = delete;

    template <typename TEvent>
    void publish_event(const TEvent &event)
    {
        dispatchEvent(event, std::type_index(typeid(TEvent)));
    }

private:
    SyncEventCenter() = default;
    static std::atomic<SyncEventCenter *> m_instance;
    static std::mutex m_creationMutex;
};

//----------------------------------------------------------------
// AsyncEventCenter
//----------------------------------------------------------------
class EVENTSYSTEM_API AsyncEventCenter : public EventRegistry
{
public:
    static AsyncEventCenter &instance();
    static void destroy();

    AsyncEventCenter(const AsyncEventCenter &) = delete;
    AsyncEventCenter &operator=(const AsyncEventCenter &) = delete;

    ~AsyncEventCenter();

    template <typename TEvent>
    void publish_event(const TEvent &event)
    {
        publish_event_at(event, std::chrono::steady_clock::now());
    }

    template <typename TEvent>
    void publish_event_delayed(const TEvent &event, std::chrono::milliseconds delay)
    {
        publish_event_at(event, std::chrono::steady_clock::now() + delay);
    }

    template <typename TEvent>
    void publish_event_at(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
    {
        ensureWorkerThread();
        ScheduledEvent newEvent{timePoint, event, std::type_index(typeid(TEvent))};
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_pendingEvents.push_back(std::move(newEvent));
        }
        m_condVar.notify_one();
    }

    void cancelAllEvents();

private:
    AsyncEventCenter() = default;

    void ensureWorkerThread();
    void stopWorkerThread();

    struct ScheduledEvent
    {
        std::chrono::steady_clock::time_point executionTime;
        std::any eventData;
        std::type_index eventType;

        bool operator>(const ScheduledEvent &other) const
        {
            return executionTime > other.executionTime;
        }
    };

    void processEvents();

    std::vector<ScheduledEvent> m_pendingEvents;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent>> m_scheduledQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condVar;

    std::thread m_workerThread;
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_threadRunning{false};
    std::mutex m_threadMutex;

    static std::atomic<AsyncEventCenter *> m_instance;
    static std::mutex m_creationMutex;
};

using EventCenter = AsyncEventCenter;

//----------------------------------------------------------------
// Helper "Tool" functions
//----------------------------------------------------------------

// --- Synchronous Publishing ---
template <typename TEvent>
void publish_event_sync(const TEvent &event)
{
    eventsystem::SyncEventCenter::instance().publish_event(event);
}

// --- Asynchronous Publishing ---
template <typename TEvent>
void publish_event_async(const TEvent &event)
{
    eventsystem::AsyncEventCenter::instance().publish_event(event);
}

template <typename TEvent>
void publish_event_delayed_async(const TEvent &event, std::chrono::milliseconds delay)
{
    eventsystem::AsyncEventCenter::instance().publish_event_delayed(event, delay);
}

template <typename TEvent>
void publish_event_at_async(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    eventsystem::AsyncEventCenter::instance().publish_event_at(event, timePoint);
}

// --- Default/Legacy Aliases ---
template <typename TEvent>
void publish_event(const TEvent &event)
{
    publish_event_async(event);
}

template <typename TEvent>
void publish_event_delayed(const TEvent &event, std::chrono::milliseconds delay)
{
    publish_event_delayed_async(event, delay);
}

template <typename TEvent>
void publish_event_at(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    publish_event_at_async(event, timePoint);
}

inline void cancelAllEvents()
{
    eventsystem::EventCenter::instance().cancelAllEvents();
}

// --- Helper "Tool" functions for convenient EventCenter registration ---

// Unified Subscription (Async/Sync agnostic)
template <typename TEvent>
eventsystem::SubscriptionHandle subscribe_event(std::function<void(const TEvent&)> callback)
{
    return eventsystem::EventRegistry::registerHandler<TEvent>(std::move(callback));
}

inline void unsubscribe_event(eventsystem::SubscriptionHandle handle)
{
    eventsystem::EventRegistry::unregisterHandler(handle);
}

// --- Static Handler Registration ---
template <typename TEvent>
eventsystem::SubscriptionHandle registerStaticEventHandler()
{
    return eventsystem::EventRegistry::registerHandler<TEvent>(&TEvent::handle);
}

} // namespace eventsystem
