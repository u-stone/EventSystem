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
// EventRegistry: Manages event subscriptions and dispatching logic.
// This is a base class for specific EventCenter implementations.
//----------------------------------------------------------------
class EventRegistry
{
public:
    virtual ~EventRegistry() = default;

    // --- IEventHandler-based Subscription ---

    // Registers a handler with strong ownership (the default).
    // The registry will share ownership, keeping the handler alive until unregistered.
    // Use this for "fire-and-forget" registration.
    template <typename TEvent>
    void registerHandler(const std::shared_ptr<IEventHandler> &handler)
    {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_interfaceHandlers[eventType].strongRefs.push_back(handler);
    }

    // Registers a handler with weak ownership.
    // The registry only observes the handler and will not keep it alive.
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

protected:
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

        // Helper lambda to execute handlers safely with exception isolation and timing checks.
        auto safeInvoke = [&](const auto &action, const char *typeLabel)
        {
            try
            {
                auto start = std::chrono::steady_clock::now();
                action();
                auto end = std::chrono::steady_clock::now();

                // Simple heuristic: if a handler takes > 500ms, warn about it.
                // This helps identify potential deadlocks or performance bottlenecks.
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if (duration.count() > 500)
                {
                    std::cerr << "[EventSystem] Warning: " << typeLabel << " took " << duration.count()
                              << "ms to execute. Check for slow code or infinite loops." << std::endl;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "[EventSystem] Exception in " << typeLabel << ": " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "[EventSystem] Unknown exception in " << typeLabel << "." << std::endl;
            }
        };

        // Execute handlers outside the lock.
        for (const auto &handler : strong_handlers)
        {
            safeInvoke([&]() { handler->handle(eventData); }, "StrongHandler");
        }
        for (const auto &weak_handler : weak_handlers)
        {
            if (auto handler = weak_handler.lock())
            {
                safeInvoke([&]() { handler->handle(eventData); }, "WeakHandler");
            }
        }
        for (const auto &callback : callbacks)
        {
            safeInvoke([&]() { callback(eventData); }, "CallbackHandler");
        }
    }

    // Holds both strong and weak references for interface-based handlers
    struct InterfaceHandlers
    {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };

    using GenericCallback = std::function<void(const std::any &)>;

    // Storage for interface-based subscriptions
    std::map<std::type_index, InterfaceHandlers>
        m_interfaceHandlers;

    // Storage for callback-based subscriptions
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> m_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> m_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> m_nextSubscriptionId{0};
    std::mutex m_registryMutex;
};

//----------------------------------------------------------------
// SyncEventCenter: A simple, single-threaded event center.
// Events are dispatched immediately on the calling thread.
//----------------------------------------------------------------
class SyncEventCenter : public EventRegistry
{
public:
    static SyncEventCenter &instance()
    {
        SyncEventCenter *ptr = m_instance.load(std::memory_order_acquire);
        if (!ptr)
        {
            std::lock_guard<std::mutex> lock(m_creationMutex);
            ptr = m_instance.load(std::memory_order_relaxed);
            if (!ptr)
            {
                ptr = new SyncEventCenter();
                m_instance.store(ptr, std::memory_order_release);
            }
        }
        return *ptr;
    }

    static void destroy()
    {
        std::lock_guard<std::mutex> lock(m_creationMutex);
        SyncEventCenter *ptr = m_instance.load(std::memory_order_acquire);
        if (ptr)
        {
            delete ptr;
            m_instance.store(nullptr, std::memory_order_release);
        }
    }

    SyncEventCenter(const SyncEventCenter &) = delete;
    SyncEventCenter &operator=(const SyncEventCenter &) = delete;

    // Publishes an event for immediate processing.
    template <typename TEvent>
    void publish_event(const TEvent &event)
    {
        dispatchEvent(event, std::type_index(typeid(TEvent)));
    }

private:
    SyncEventCenter() = default;
    inline static std::atomic<SyncEventCenter *> m_instance{nullptr};
    inline static std::mutex m_creationMutex;
};

//----------------------------------------------------------------
// AsyncEventCenter: A multi-threaded event center.
// Events are queued and dispatched by a background worker thread.
// Supports delayed and scheduled events.
//----------------------------------------------------------------
class AsyncEventCenter : public EventRegistry
{
public:
    static AsyncEventCenter &instance()
    {
        AsyncEventCenter *ptr = m_instance.load(std::memory_order_acquire);
        if (!ptr)
        {
            std::lock_guard<std::mutex> lock(m_creationMutex);
            ptr = m_instance.load(std::memory_order_relaxed);
            if (!ptr)
            {
                ptr = new AsyncEventCenter();
                m_instance.store(ptr, std::memory_order_release);
            }
        }
        return *ptr;
    }

    static void destroy()
    {
        std::lock_guard<std::mutex> lock(m_creationMutex);
        AsyncEventCenter *ptr = m_instance.load(std::memory_order_acquire);
        if (ptr)
        {
            delete ptr;
            m_instance.store(nullptr, std::memory_order_release);
        }
    }

    AsyncEventCenter(const AsyncEventCenter &) = delete;
    AsyncEventCenter &operator=(const AsyncEventCenter &) = delete;

    ~AsyncEventCenter()
    {
        cancelAllEvents();
        stopWorkerThread();
    }

    // Publishes an event for asynchronous processing.
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
        ensureWorkerThread();
        ScheduledEvent newEvent{timePoint, event, std::type_index(typeid(TEvent))};
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_pendingEvents.push_back(std::move(newEvent));
        }
        m_condVar.notify_one();
    }

    void cancelAllEvents()
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingEvents.clear();
        m_scheduledQueue = {};
    }

private:
    AsyncEventCenter() = default;

    // Ensures the worker thread is running, creating it if necessary.
    // This uses a double-checked locking pattern. A std::mutex is chosen over
    // std::call_once because the worker thread has a lifecycle that includes
    // explicit destruction (~AsyncEventCenter -> stopWorkerThread). The mutex
    // correctly serializes both creation and destruction, preventing race
    // conditions where the thread might be stopped while it's still being
    // created. std::call_once is ideal for resources that are initialized
    // once and never destroyed until program termination.
    void ensureWorkerThread()
    {
        if (!m_threadRunning)
        {
            std::lock_guard<std::mutex> lock(m_threadMutex);
            // The mutex synchronizes access for both creation and destruction.
            if (!m_threadRunning)
            {
                m_done = false;
                m_workerThread = std::thread(&AsyncEventCenter::processEvents, this);
                m_threadRunning = true;
            }
        }
    }

    void stopWorkerThread()
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (m_threadRunning)
        {
            m_done = true;
            m_condVar.notify_all();
            if (m_workerThread.joinable())
            {
                m_workerThread.join();
            }
            m_threadRunning = false;
        }
    }

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

    void processEvents()
    {
        while (true)
        {
            std::vector<ScheduledEvent> eventsToDispatch;

            {
                std::unique_lock<std::mutex> lock(m_queueMutex);

                if (!m_pendingEvents.empty())
                {
                    for (auto &evt : m_pendingEvents)
                    {
                        m_scheduledQueue.push(std::move(evt));
                    }
                    m_pendingEvents.clear();
                }

                if (m_scheduledQueue.empty())
                {
                    m_condVar.wait(lock, [this] { return m_done || !m_pendingEvents.empty(); });
                }
                else
                {
                    m_condVar.wait_until(lock, m_scheduledQueue.top().executionTime, [this] { return m_done || !m_pendingEvents.empty(); });
                }

                if (m_done && m_pendingEvents.empty() && m_scheduledQueue.empty())
                {
                    return;
                }

                if (!m_pendingEvents.empty())
                {
                    continue;
                }

                auto now = std::chrono::steady_clock::now();
                while (!m_scheduledQueue.empty() && m_scheduledQueue.top().executionTime <= now)
                {
                    eventsToDispatch.push_back(m_scheduledQueue.top());
                    m_scheduledQueue.pop();
                }
            }

            for (const auto &scheduledEvent : eventsToDispatch)
            {
                dispatchEvent(scheduledEvent.eventData, scheduledEvent.eventType);
            }
        }
    }

    std::vector<ScheduledEvent> m_pendingEvents;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent>> m_scheduledQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condVar;

    std::thread m_workerThread;
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_threadRunning{false};
    std::mutex m_threadMutex;

    inline static std::atomic<AsyncEventCenter *> m_instance{nullptr};
    inline static std::mutex m_creationMutex;
};

// Alias for backward compatibility, or default to AsyncEventCenter
using EventCenter = AsyncEventCenter;

//----------------------------------------------------------------
// Helper "Tool" functions to publish events from anywhere.
//
// These are the primary way to send events into the system. They offer
// a "fire-and-forget" mechanism. The function calls are non-blocking
// and return immediately, while the event is queued for asynchronous
// processing by the EventCenter's worker thread.
//----------------------------------------------------------------

// --- Synchronous Publishing ---

// Publishes an event for immediate synchronous processing on the calling thread.
template <typename TEvent>
void publish_event_sync(const TEvent &event)
{
    SyncEventCenter::instance().publish_event(event);
}

// --- Asynchronous Publishing ---

// Publishes an event for asynchronous processing (default).
template <typename TEvent>
void publish_event_async(const TEvent &event)
{
    AsyncEventCenter::instance().publish_event(event);
}

// Publishes an event to be processed after a specified delay (asynchronous).
template <typename TEvent>
void publish_event_delayed_async(const TEvent &event, std::chrono::milliseconds delay)
{
    AsyncEventCenter::instance().publish_event_delayed(event, delay);
}

// Publishes an event to be processed at a specific time point (asynchronous).
template <typename TEvent>
void publish_event_at_async(const TEvent &event, const std::chrono::steady_clock::time_point &timePoint)
{
    AsyncEventCenter::instance().publish_event_at(event, timePoint);
}

// --- Default/Legacy Aliases (Async by default) ---

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

// Cancels all pending and scheduled events.
inline void cancelAllEvents()
{
    EventCenter::instance().cancelAllEvents();
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
