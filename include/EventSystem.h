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

// A unique identifier for a callback subscription, used for unregistering.
using SubscriptionHandle = size_t;

//----------------------------------------------------------------
// Base class for all event handlers.
//
// To create a custom event handler, inherit from this class and
// override the `handle` method. Inside the `handle` method, use
// `std::any_cast` to check for the specific event types you are
// interested in.
//
// Example:
//
//   struct MyCustomEvent { int value; };
//
//   class MyHandler : public IEventHandler {
//   public:
//       void handle(const std::any& eventData) override {
//           // Check if the received event is of type MyCustomEvent
//           if (auto* event = std::any_cast<MyCustomEvent>(&eventData)) {
//               // If it is, process it.
//               std::cout << "Handled MyCustomEvent with value: " << event->value << std::endl;
//           }
//           // You can add more 'else if' blocks to handle other event types.
//       }
//   };
//
//----------------------------------------------------------------
class IEventHandler
{
public:
    virtual ~IEventHandler() = default;
    virtual void handle(const std::any &eventData) = 0;
};

//----------------------------------------------------------------
// The EventCenter is a singleton that manages and dispatches events asynchronously.
// It supports both class-based (IEventHandler) and function-based (callback) handlers.
//----------------------------------------------------------------
class EventCenter
{
public:
    // Provides access to the singleton instance.
    static EventCenter &instance()
    {
        static EventCenter center;
        return center;
    }

    // Deleted copy constructor and assignment operator.
    EventCenter(const EventCenter &) = delete;
    EventCenter &operator=(const EventCenter &) = delete;

    ~EventCenter()
    {
        m_done = true;
        m_condVar.notify_one();
        if (m_workerThread.joinable())
        {
            m_workerThread.join();
        }
    }

    // --- IEventHandler-based Subscription ---

    // Registers a handler with strong ownership (the default).
    // The EventCenter will share ownership, keeping the handler alive until unregistered.
    // Use this for "fire-and-forget" registration.
    template <typename TEvent>
    void registerHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].strongRefs.push_back(handler);
    }

    // Registers a handler with weak ownership.
    // The EventCenter only observes the handler and will not keep it alive.
    // Use this when the handler's lifetime is managed externally to prevent potential memory leaks.
    template <typename TEvent>
    void registerWeakHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].weakRefs.push_back(handler);
    }

    // Unregisters a handler from both strong and weak lists.
    template <typename TEvent>
    void unregisterHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        auto it = m_interfaceHandlers.find(eventType);
        if (it != m_interfaceHandlers.end())
        {
            auto &handlerGroup = it->second;
            // Remove from strong list
            handlerGroup.strongRefs.erase(
                std::remove(handlerGroup.strongRefs.begin(), handlerGroup.strongRefs.end(), handler),
                handlerGroup.strongRefs.end());

            // Remove from weak list
            handlerGroup.weakRefs.erase(
                std::remove_if(handlerGroup.weakRefs.begin(), handlerGroup.weakRefs.end(),
                               [&handler](const std::weak_ptr<IEventHandler> &weak)
                               {
                                   return weak.expired() || weak.lock() == handler;
                               }),
                handlerGroup.weakRefs.end());
        }
    }

    // --- Callback-based Subscription ---
    template <typename TEvent>
    SubscriptionHandle registerHandler(std::function<void(const TEvent &)> callback)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        SubscriptionHandle handle = m_nextSubscriptionId++;
        // Create a wrapper that casts std::any to the correct event type.
        auto wrapper = [callback](const std::any &eventData)
        {
            if (auto *event = std::any_cast<TEvent>(&eventData))
            {
                callback(*event);
            }
        };

        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_callbackHandlers[eventType][handle] = wrapper;
        m_handleToEventTypeMap.emplace(handle, eventType); // For quick unregistering
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

    // Unregisters all handlers (both class-based and callback-based) for a specific event type.
    template <typename TEvent>
    void unregisterAllHandlers()
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);

        // 1. Clear callback handlers for this event type
        auto it_cb = m_callbackHandlers.find(eventType);
        if (it_cb != m_callbackHandlers.end())
        {
            // Also remove the corresponding entries from the reverse map
            for (const auto &[handle, func] : it_cb->second)
            {
                m_handleToEventTypeMap.erase(handle);
            }
            m_callbackHandlers.erase(it_cb);
        }

        // 2. Clear interface handlers for this event type
        m_interfaceHandlers.erase(eventType);
    }

    // --- Event Posting ---

    // Publishes an event for immediate asynchronous processing.
    template <typename TEvent>
    void publish_event(const TEvent &event)
    {
        publish_event_at(event, std::chrono::steady_clock::now());
    }

    // Publishes an event to be processed after a specified delay.
    template <typename TEvent>
    void publish_event_delayed(const TEvent &event, std::chrono::milliseconds delay)
    {
        publish_event_at(event, std::chrono::steady_clock::now() + delay);
    }

    // Publishes an event to be processed at a specific time point.
    template <typename TEvent>
    void publish_event_at(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
    {
        std::call_once(m_initFlag, &EventCenter::startWorker, this);
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_pendingEvents.push_back({timePoint, event, std::type_index(typeid(TEvent))});
        }
        m_condVar.notify_one();
    }

private:
    // Internal struct to hold event data along with its scheduled execution time.
    struct ScheduledEvent
    {
        std::chrono::steady_clock::time_point executionTime;
        std::any eventData;
        std::type_index eventType;

        // Comparison for the priority queue to make it a min-heap.
        bool operator>(const ScheduledEvent &other) const
        {
            return executionTime > other.executionTime;
        }
    };

    // Holds both strong and weak references for interface-based handlers
    struct InterfaceHandlers
    {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };

    using GenericCallback = std::function<void(const std::any &)>;

    // Private constructor for singleton pattern.
    EventCenter()
        : m_done(false)
    {
    }

    void startWorker()
    {
        m_workerThread = std::thread(&EventCenter::processEvents, this);
    }

    void processEvents()
    {
        // Local priority queue, accessed only by the worker thread, no locking required.
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent>> localEventQueue;

        while (true)
        {
            std::vector<ScheduledEvent> eventsToDispatch;
            std::vector<ScheduledEvent> newEvents;

            {
                std::unique_lock<std::mutex> lock(m_queueMutex);

                // If there are no pending scheduled events locally, wait for new events or exit signal.
                if (localEventQueue.empty())
                {
                    m_condVar.wait(lock, [this]
                                   { return m_done || !m_pendingEvents.empty(); });
                }
                else
                {
                    // If there are local scheduled events, wait until the earliest event expires or a new event is added.
                    auto nextTime = localEventQueue.top().executionTime;
                    m_condVar.wait_until(lock, nextTime, [this]
                                         { return m_done || !m_pendingEvents.empty(); });
                }

                if (m_done && m_pendingEvents.empty() && localEventQueue.empty())
                {
                    return;
                }

                // Fast swap: "Steal" externally submitted events to a local variable, minimizing lock holding time.
                if (!m_pendingEvents.empty())
                {
                    newEvents.swap(m_pendingEvents);
                }
            } // Lock is released here.

            // Merge new events into the local priority queue outside the lock.
            for (const auto &evt : newEvents)
            {
                localEventQueue.push(evt);
            }

            // Check and extract expired events.
            auto now = std::chrono::steady_clock::now();
            while (!localEventQueue.empty() && localEventQueue.top().executionTime <= now)
            {
                eventsToDispatch.push_back(localEventQueue.top());
                localEventQueue.pop();
            }

            // If shutting down and no immediate tasks, exit (prevent busy waiting due to future events).
            if (m_done && eventsToDispatch.empty() && newEvents.empty())
            {
                return;
            }

            for (const auto &scheduledEvent : eventsToDispatch)
            {
                dispatchEvent(scheduledEvent.eventData, scheduledEvent.eventType);
            }
        }
    }

    void dispatchEvent(const std::any &eventData, const std::type_index &eventType)
    {
        std::vector<std::shared_ptr<IEventHandler>> strong_handlers;
        std::vector<std::weak_ptr<IEventHandler>> weak_handlers;
        std::vector<GenericCallback> callbacks;

        {
            std::lock_guard<std::mutex> lock(m_registryMutex);

            // 1. Collect IEventHandler subscribers
            auto it_ih = m_interfaceHandlers.find(eventType);
            if (it_ih != m_interfaceHandlers.end())
            {
                strong_handlers = it_ih->second.strongRefs; // Copy
                weak_handlers = it_ih->second.weakRefs;     // Copy
            }

            // 2. Collect callback subscribers
            auto it_cb = m_callbackHandlers.find(eventType);
            if (it_cb != m_callbackHandlers.end())
            {
                for (const auto &pair : it_cb->second)
                {
                    callbacks.push_back(pair.second);
                }
            }
        }

        // Execute handlers outside the lock.
        try
        {
            for (const auto &handler : strong_handlers)
            {
                handler->handle(eventData);
            }
            for (const auto &weak_handler : weak_handlers)
            {
                if (auto handler = weak_handler.lock())
                {
                    handler->handle(eventData);
                }
            }
            for (const auto &callback : callbacks)
            {
                callback(eventData);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception during event dispatch: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception during event dispatch." << std::endl;
        }
    }

    // Storage for interface-based subscriptions
    std::map<std::type_index, InterfaceHandlers>
        m_interfaceHandlers;

    // Storage for callback-based subscriptions
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> m_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> m_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> m_nextSubscriptionId{0};

    // Buffer for receiving new events (Pending Queue).
    std::vector<ScheduledEvent> m_pendingEvents;
    std::mutex m_queueMutex;
    std::mutex m_registryMutex;
    std::condition_variable m_condVar;

    std::thread m_workerThread;
    std::atomic<bool> m_done;
    std::once_flag m_initFlag;
};

//----------------------------------------------------------------
// Helper "Tool" functions to publish events from anywhere.
//
// These are the primary way to send events into the system. They offer
// a "fire-and-forget" mechanism. The function calls are non-blocking
// and return immediately, while the event is queued for asynchronous
// processing by the EventCenter's worker thread.
//----------------------------------------------------------------

// Publishes an event for immediate asynchronous processing.
template <typename TEvent>
void publish_event(const TEvent &event)
{
    EventCenter::instance().publish_event(event);
}

// Publishes an event to be processed after a specified delay.
template <typename TEvent>
void publish_event_delayed(const TEvent &event, std::chrono::milliseconds delay)
{
    EventCenter::instance().publish_event_delayed(event, delay);
}

// Publishes an event to be processed at a specific time point.
template <typename TEvent>
void publish_event_at(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    EventCenter::instance().publish_event_at(event, timePoint);
}

//----------------------------------------------------------------
// Helper "Tool" function for self-registering stateless events.
//
// This provides an elegant pattern for simple, stateless handlers.
// Define a static `handle` method inside your event struct, and this
// function will automatically register it as the handler.
//
// Usage:
//
//   struct MyStatelessEvent {
//       static void handle(const MyStatelessEvent& event) { /*...*/ }
//   };
//
//   registerStaticEventHandler<MyStatelessEvent>();
//
//----------------------------------------------------------------
template <typename TEvent>
SubscriptionHandle registerStaticEventHandler()
{
    // Creates a std::function from the static handle method and registers it.
    return EventCenter::instance().registerHandler<TEvent>(&TEvent::handle);
}
