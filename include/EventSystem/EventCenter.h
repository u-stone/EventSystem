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
// EventRegistry: Manages shared event subscriptions (Static Backend).
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

    static void PrintSubscriptions();
    static void Reset();

    // Internal helper for dispatching (used by EventCenter)
    static void DispatchEvent(const std::any &eventData, const std::type_index &eventType);

private:
    static void RegisterInterfaceHandler(const std::type_index& type, const std::shared_ptr<IEventHandler>& handler, bool isWeak);
    static void UnregisterInterfaceHandler(const std::type_index& type, const std::shared_ptr<IEventHandler>& handler);
    
    using GenericCallback = std::function<void(const std::any &)>;
    static SubscriptionHandle RegisterCallbackHandler(const std::type_index& type, GenericCallback callback);
    static void UnregisterAllHandlers(const std::type_index& type);
};

//----------------------------------------------------------------
// EventCenter: Unified Event System (Async & Sync)
//----------------------------------------------------------------
class EVENTSYSTEM_API EventCenter
{
public:
    static EventCenter &Instance();
    
    /**
     * @brief Explicitly destroys the singleton instance.
     * Important for avoiding deadlocks on Windows DLL unload if the worker thread is running.
     */
    static void Destroy();

    EventCenter(const EventCenter &) = delete;
    EventCenter &operator=(const EventCenter &) = delete;

    ~EventCenter();

    void SetPublishMode(PublishMode mode);
    PublishMode GetPublishMode() const;

    template <typename TEvent>
    void PublishSync(const TEvent &event)
    {
        EventRegistry::DispatchEvent(event, std::type_index(typeid(TEvent)));
    }

    template <typename TEvent>
    void PublishAsync(const TEvent &event)
    {
        PublishEventInternal(event, std::type_index(typeid(TEvent)), std::chrono::steady_clock::now());
    }

    template <typename TEvent>
    void Publish(const TEvent &event)
    {
        if (GetPublishMode() == PublishMode::Sync) {
            PublishSync(event);
        } else {
            PublishAsync(event);
        }
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
    EventCenter();
    
    void PublishEventInternal(const std::any& eventData, const std::type_index& type, const std::chrono::steady_clock::time_point& timePoint);

    // PIMPL for Async implementation details
    struct Impl;
    Impl* m_impl; 
};

//----------------------------------------------------------------
// Helper Functions
//----------------------------------------------------------------

// --- Synchronous Publishing ---
template <typename TEvent>
void PublishEventSync(const TEvent &event)
{
    eventsystem::EventCenter::Instance().PublishSync(event);
}

// --- Asynchronous Publishing ---
template <typename TEvent>
void PublishEventAsync(const TEvent &event)
{
    eventsystem::EventCenter::Instance().PublishAsync(event);
}

template <typename TEvent>
void PublishEventDelayedAsync(const TEvent &event, std::chrono::milliseconds delay)
{
    eventsystem::EventCenter::Instance().PublishEventDelayed(event, delay);
}

template <typename TEvent>
void PublishEventAtAsync(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    eventsystem::EventCenter::Instance().PublishEventAt(event, timePoint);
}

// --- Default Aliases ---
template <typename TEvent>
void PublishEvent(const TEvent &event)
{
    eventsystem::EventCenter::Instance().Publish(event);
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

inline void UnregisterStaticEventHandler(eventsystem::SubscriptionHandle handle)
{
    eventsystem::EventRegistry::UnregisterHandler(handle);
}

template <typename TEvent>
inline void UnregisterStaticEventHandler()
{
    eventsystem::EventRegistry::UnregisterAllHandlers<TEvent>();
}

// --- C-style API ---

inline void SetEventCenterPublishMode(eventsystem::PublishMode mode)
{
    eventsystem::EventCenter::Instance().SetPublishMode(mode);
}

inline eventsystem::PublishMode GetEventCenterPublishMode()
{
    return eventsystem::EventCenter::Instance().GetPublishMode();
}

} // namespace eventsystem
