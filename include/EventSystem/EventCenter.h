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
        registerHandlerImpl(std::type_index(typeid(TEvent)), handler);
    }

    template <typename TEvent>
    static void registerWeakHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        registerWeakHandlerImpl(std::type_index(typeid(TEvent)), handler);
    }

    template <typename TEvent>
    static void unregisterHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        unregisterHandlerImpl(std::type_index(typeid(TEvent)), handler);
    }

    // --- Callback-based Subscription (Static / Shared) ---
    template <typename TEvent>
    static SubscriptionHandle registerHandler(std::function<void(const TEvent &)> callback)
    {
        auto wrapper = [callback](const std::any &eventData)
        {
            if (auto *event = std::any_cast<TEvent>(&eventData))
            {
                callback(*event);
            }
        };
        return registerCallbackImpl(std::type_index(typeid(TEvent)), wrapper);
    }

    static void unregisterHandler(SubscriptionHandle handle);

    template <typename TEvent>
    static void unregisterAllHandlers()
    {
        unregisterAllHandlersImpl(std::type_index(typeid(TEvent)));
    }

    // Clears all subscriptions (for testing or system reset)
    static void reset();

protected:
    static void dispatchEvent(const std::any &eventData, const std::type_index &eventType);

    using GenericCallback = std::function<void(const std::any &)>;

    // Internal helpers to hide STL types from DLL interface
    static void registerHandlerImpl(std::type_index type, const std::shared_ptr<IEventHandler>& handler);
    static void registerWeakHandlerImpl(std::type_index type, const std::shared_ptr<IEventHandler>& handler);
    static void unregisterHandlerImpl(std::type_index type, const std::shared_ptr<IEventHandler>& handler);
    static SubscriptionHandle registerCallbackImpl(std::type_index type, GenericCallback callback);
    static void unregisterAllHandlersImpl(std::type_index type);
};

//----------------------------------------------------------------
// SyncEventCenter
//----------------------------------------------------------------
class EVENTSYSTEM_API SyncEventCenter : public EventRegistry
{
public:
    static SyncEventCenter &instance();
    // destroy is deprecated with Meyers Singleton but kept for API compatibility (will be no-op or reset)
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
        scheduleEvent(event, std::type_index(typeid(TEvent)), timePoint);
    }

    void cancelAllEvents();

private:
    void scheduleEvent(std::any event, std::type_index type, std::chrono::steady_clock::time_point timePoint);

    AsyncEventCenter();

    // Pimpl idiom to hide implementation details and STL members
    struct Impl;
#pragma warning(push)
#pragma warning(disable: 4251)
    std::unique_ptr<Impl> m_impl;
#pragma warning(pop)
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
