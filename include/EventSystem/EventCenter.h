#pragma once

#include "Export.h"
#include <any>
#include <typeindex>
#include <memory>
#include <functional>
#include <chrono>

namespace eventsystem {

using SubscriptionHandle = size_t;

//----------------------------------------------------------------
// IEventHandler
//----------------------------------------------------------------
class EVENTSYSTEM_API IEventHandler
{
public:
    virtual ~IEventHandler() = default;
    virtual void Handle(const std::any &eventData) = 0;
};

//----------------------------------------------------------------
// EventRegistry: Manages shared event subscriptions.
//----------------------------------------------------------------
class EVENTSYSTEM_API EventRegistry
{
public:
    virtual ~EventRegistry() = default;

    template <typename TEvent>
    static void RegisterHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        RegisterInterfaceHandler(std::type_index(typeid(TEvent)), handler, false);
    }

    template <typename TEvent>
    static void RegisterWeakHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        RegisterInterfaceHandler(std::type_index(typeid(TEvent)), handler, true);
    }

    template <typename TEvent>
    static void UnregisterHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        UnregisterInterfaceHandler(std::type_index(typeid(TEvent)), handler);
    }

    template <typename TEvent>
    static SubscriptionHandle RegisterHandler(std::function<void(const TEvent &)> callback)
    {
        auto wrapper = [callback](const std::any &eventData)
        {
            if (auto *event = std::any_cast<TEvent>(&eventData))
            {
                callback(*event);
            }
        };
        return RegisterCallbackHandler(std::type_index(typeid(TEvent)), std::move(wrapper));
    }

    static void UnregisterHandler(SubscriptionHandle handle);

    template <typename TEvent>
    static void UnregisterAllHandlers()
    {
        UnregisterAllHandlers(std::type_index(typeid(TEvent)));
    }

    static void Reset();

protected:
    static void DispatchEvent(const std::any &eventData, const std::type_index &eventType);

private:
    static void RegisterInterfaceHandler(const std::type_index& type, const std::shared_ptr<IEventHandler>& handler, bool isWeak);
    static void UnregisterInterfaceHandler(const std::type_index& type, const std::shared_ptr<IEventHandler>& handler);
    
    using GenericCallback = std::function<void(const std::any &)>;
    static SubscriptionHandle RegisterCallbackHandler(const std::type_index& type, GenericCallback callback);
    static void UnregisterAllHandlers(const std::type_index& type);
};

//----------------------------------------------------------------
// SyncEventCenter
//----------------------------------------------------------------
class EVENTSYSTEM_API SyncEventCenter : public EventRegistry
{
public:
    static SyncEventCenter &Instance();
    static void Destroy();

    SyncEventCenter(const SyncEventCenter &) = delete;
    SyncEventCenter &operator=(const SyncEventCenter &) = delete;

    template <typename TEvent>
    void PublishEvent(const TEvent &event)
    {
        DispatchEvent(event, std::type_index(typeid(TEvent)));
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
    static AsyncEventCenter &Instance();
    static void Destroy();

    AsyncEventCenter(const AsyncEventCenter &) = delete;
    AsyncEventCenter &operator=(const AsyncEventCenter &) = delete;

    ~AsyncEventCenter();

    template <typename TEvent>
    void PublishEvent(const TEvent &event)
    {
        PublishEventInternal(event, std::type_index(typeid(TEvent)), std::chrono::steady_clock::now());
    }

    template <typename TEvent>
    void PublishEventDelayed(const TEvent &event, std::chrono::milliseconds delay)
    {
        PublishEventInternal(event, std::type_index(typeid(TEvent)), std::chrono::steady_clock::now() + delay);
    }

    template <typename TEvent>
    void PublishEventAt(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
    {
        PublishEventInternal(event, std::type_index(typeid(TEvent)), timePoint);
    }

    void CancelAllEvents();

private:
    AsyncEventCenter();
    
    void PublishEventInternal(const std::any& eventData, const std::type_index& type, const std::chrono::steady_clock::time_point& timePoint);

    struct Impl;
    Impl* m_impl; 
};

using EventCenter = AsyncEventCenter;

//----------------------------------------------------------------
// Helper Functions
//----------------------------------------------------------------

// --- Synchronous Publishing ---
template <typename TEvent>
void PublishEventSync(const TEvent &event)
{
    eventsystem::SyncEventCenter::Instance().PublishEvent(event);
}

// --- Asynchronous Publishing ---
template <typename TEvent>
void PublishEventAsync(const TEvent &event)
{
    eventsystem::AsyncEventCenter::Instance().PublishEvent(event);
}

template <typename TEvent>
void PublishEventDelayedAsync(const TEvent &event, std::chrono::milliseconds delay)
{
    eventsystem::AsyncEventCenter::Instance().PublishEventDelayed(event, delay);
}

template <typename TEvent>
void PublishEventAtAsync(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    eventsystem::AsyncEventCenter::Instance().PublishEventAt(event, timePoint);
}

// --- Default Aliases ---
template <typename TEvent>
void PublishEvent(const TEvent &event)
{
    PublishEventAsync(event);
}

template <typename TEvent>
void PublishEventDelayed(const TEvent &event, std::chrono::milliseconds delay)
{
    PublishEventDelayedAsync(event, delay);
}

template <typename TEvent>
void PublishEventAt(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    PublishEventAtAsync(event, timePoint);
}

inline void CancelAllEvents()
{
    eventsystem::EventCenter::Instance().CancelAllEvents();
}

template <typename TEvent>
eventsystem::SubscriptionHandle SubscribeEvent(std::function<void(const TEvent&)> callback)
{
    return eventsystem::EventRegistry::RegisterHandler<TEvent>(std::move(callback));
}

inline void UnsubscribeEvent(eventsystem::SubscriptionHandle handle)
{
    eventsystem::EventRegistry::UnregisterHandler(handle);
}

template <typename TEvent>
eventsystem::SubscriptionHandle RegisterStaticEventHandler()
{
    return eventsystem::EventRegistry::RegisterHandler<TEvent>(&TEvent::Handle);
}

} // namespace eventsystem