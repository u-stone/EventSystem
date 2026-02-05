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
// EventCenter: Unified Event System (Async & Sync)
//----------------------------------------------------------------
class EVENTSYSTEM_API EventCenter
{
public:
    static EventCenter &Instance();
    
    /**
     * @brief Explicitly destroys the singleton instance and clears all subscriptions.
     */
    static void Destroy();

    EventCenter(const EventCenter &) = delete;
    EventCenter &operator=(const EventCenter &) = delete;

    ~EventCenter();

    // --- Configuration ---
    void SetPublishMode(PublishMode mode);
    PublishMode GetPublishMode() const;

    // --- Subscription API ---

    template <typename TEvent>
    SubscriptionHandle Subscribe(const std::shared_ptr<IEventHandler> &handler)
    {
        return SubscribeInternal(std::type_index(typeid(TEvent)), handler, false);
    }

    template <typename TEvent>
    SubscriptionHandle SubscribeWeak(const std::shared_ptr<IEventHandler> &handler)
    {
        return SubscribeInternal(std::type_index(typeid(TEvent)), handler, true);
    }

    template <typename TEvent>
    SubscriptionHandle Subscribe(std::function<void(const TEvent &)> callback)
    {
        auto wrapper = [callback](const std::any &eventData)
        {
            if (auto *event = std::any_cast<TEvent>(&eventData))
            {
                callback(*event);
            }
        };
        return SubscribeCallbackInternal(std::type_index(typeid(TEvent)), std::move(wrapper));
    }

    void Unsubscribe(SubscriptionHandle handle);

    template <typename TEvent>
    void UnsubscribeAll()
    {
        UnsubscribeAllInternal(std::type_index(typeid(TEvent)));
    }

    void Reset();
    void PrintSubscriptions();

    // --- Publishing API ---

    template <typename TEvent>
    void PublishSync(const TEvent &event)
    {
        DispatchEventInternal(event, std::type_index(typeid(TEvent)));
    }

    template <typename TEvent>
    void PublishAsync(const TEvent &event)
    {
        EnqueueEventInternal(event, std::type_index(typeid(TEvent)), std::chrono::steady_clock::now());
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
        EnqueueEventInternal(event, std::type_index(typeid(TEvent)), 
                             std::chrono::steady_clock::now() + delay);
    }

    template <typename TEvent>
    void PublishEventAt(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
    {
        EnqueueEventInternal(event, std::type_index(typeid(TEvent)), timePoint);
    }

    void CancelAllEvents();

private:
    EventCenter();
    
    // Internal PIMPL helpers
    SubscriptionHandle SubscribeInternal(const std::type_index& type, 
                                         const std::shared_ptr<IEventHandler>& handler, 
                                         bool isWeak);
    
    using GenericCallback = std::function<void(const std::any &)>;
    SubscriptionHandle SubscribeCallbackInternal(const std::type_index& type, 
                                                 GenericCallback callback);
    
    void UnsubscribeAllInternal(const std::type_index& type);
    void EnqueueEventInternal(const std::any& eventData, const std::type_index& type, 
                              const std::chrono::steady_clock::time_point& timePoint);
    void DispatchEventInternal(const std::any &eventData, const std::type_index &eventType);

    struct Impl;
    Impl* m_impl; 
};

//----------------------------------------------------------------
// Helper Functions (C-style API)
//----------------------------------------------------------------

// --- Publishing ---

template <typename TEvent>
inline void PublishEventSync(const TEvent &event) 
{ 
    EventCenter::Instance().PublishSync(event); 
}

template <typename TEvent>
inline void PublishEventAsync(const TEvent &event) 
{ 
    EventCenter::Instance().PublishAsync(event); 
}

template <typename TEvent>
inline void PublishEvent(const TEvent &event) 
{ 
    EventCenter::Instance().Publish(event); 
}

template <typename TEvent>
inline void PublishEventDelayed(const TEvent &event, std::chrono::milliseconds delay) 
{ 
    EventCenter::Instance().PublishEventDelayed(event, delay); 
}

template <typename TEvent>
inline void PublishEventAt(const TEvent &event, 
                           const std::chrono::steady_clock::time_point &timePoint) 
{ 
    EventCenter::Instance().PublishEventAt(event, timePoint); 
}

inline void CancelAllEvents() 
{ 
    EventCenter::Instance().CancelAllEvents(); 
}

// --- Subscription ---

template <typename TEvent>
inline SubscriptionHandle SubscribeEvent(std::function<void(const TEvent&)> callback) 
{ 
    return EventCenter::Instance().Subscribe<TEvent>(std::move(callback)); 
}

template <typename TEvent>
inline SubscriptionHandle SubscribeEvent(const std::shared_ptr<IEventHandler>& handler) 
{ 
    return EventCenter::Instance().Subscribe<TEvent>(handler); 
}

template <typename TEvent>
inline SubscriptionHandle SubscribeEventWeak(const std::shared_ptr<IEventHandler>& handler) 
{ 
    return EventCenter::Instance().SubscribeWeak<TEvent>(handler); 
}

inline void UnsubscribeEvent(SubscriptionHandle handle) 
{ 
    EventCenter::Instance().Unsubscribe(handle); 
}

template <typename TEvent>
inline void UnsubscribeAllEvents() 
{ 
    EventCenter::Instance().UnsubscribeAll<TEvent>(); 
}

template <typename TEvent>
inline SubscriptionHandle RegisterStaticEventHandler() 
{ 
    return EventCenter::Instance().Subscribe<TEvent>(&TEvent::Handle); 
}

inline void UnregisterStaticEventHandler(SubscriptionHandle handle) 
{ 
    EventCenter::Instance().Unsubscribe(handle); 
}

template <typename TEvent>
inline void UnregisterStaticEventHandler() 
{ 
    EventCenter::Instance().UnsubscribeAll<TEvent>(); 
}

// --- Configuration & Debug ---

inline void SetEventCenterPublishMode(PublishMode mode) 
{ 
    EventCenter::Instance().SetPublishMode(mode); 
}

inline PublishMode GetEventCenterPublishMode() 
{ 
    return EventCenter::Instance().GetPublishMode(); 
}

inline void PrintEventSubscriptions() 
{ 
    EventCenter::Instance().PrintSubscriptions(); 
}

inline void ResetEventCenter() 
{ 
    EventCenter::Instance().Reset(); 
}

inline void DestroyEventCenter() 
{ 
    EventCenter::Destroy(); 
}

} // namespace eventsystem