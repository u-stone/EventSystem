#include "EventSystem/EventCenter.h"
#include <typeindex>
#include <map>
#include <mutex>
#include <atomic>
#include <vector>
#include <queue>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <iostream>

namespace eventsystem {

// =========================================================
// Internal Storage (Pimpl for Static Registry)
// =========================================================

namespace {

    struct RegistryStorage
    {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };

    using GenericCallback = std::function<void(const std::any &)>;

    // Global state for EventRegistry (Hidden from DLL interface)
    std::map<std::type_index, RegistryStorage> g_interfaceHandlers;
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> g_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> g_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> g_nextSubscriptionId{0};
    std::mutex g_registryMutex;

} // namespace

// =========================================================
// EventRegistry Implementation
// =========================================================

void EventRegistry::registerHandlerImpl(std::type_index type, const std::shared_ptr<IEventHandler>& handler)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_interfaceHandlers[type].strongRefs.push_back(handler);
}

void EventRegistry::registerWeakHandlerImpl(std::type_index type, const std::shared_ptr<IEventHandler>& handler)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_interfaceHandlers[type].weakRefs.push_back(handler);
}

void EventRegistry::unregisterHandlerImpl(std::type_index type, const std::shared_ptr<IEventHandler>& handler)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto it = g_interfaceHandlers.find(type);
    if (it != g_interfaceHandlers.end())
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

SubscriptionHandle EventRegistry::registerCallbackImpl(std::type_index type, GenericCallback callback)
{
    SubscriptionHandle handle = g_nextSubscriptionId++;
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_callbackHandlers[type][handle] = std::move(callback);
    g_handleToEventTypeMap.emplace(handle, type);
    return handle;
}

void EventRegistry::unregisterHandler(SubscriptionHandle handle)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto it = g_handleToEventTypeMap.find(handle);
    if (it != g_handleToEventTypeMap.end())
    {
        std::type_index eventType = it->second;
        g_callbackHandlers[eventType].erase(handle);
        g_handleToEventTypeMap.erase(it);
    }
}

void EventRegistry::unregisterAllHandlersImpl(std::type_index type)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto it_cb = g_callbackHandlers.find(type);
    if (it_cb != g_callbackHandlers.end())
    {
        for (const auto &[handle, func] : it_cb->second)
        {
            g_handleToEventTypeMap.erase(handle);
        }
        g_callbackHandlers.erase(it_cb);
    }
    g_interfaceHandlers.erase(type);
}

void EventRegistry::reset()
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_interfaceHandlers.clear();
    g_callbackHandlers.clear();
    g_handleToEventTypeMap.clear();
}

void EventRegistry::dispatchEvent(const std::any &eventData, const std::type_index &eventType)
{
    std::vector<std::shared_ptr<IEventHandler>> strong_handlers;
    std::vector<std::weak_ptr<IEventHandler>> weak_handlers;
    std::vector<GenericCallback> callbacks;

    {
        std::lock_guard<std::mutex> lock(g_registryMutex);

        auto it_ih = g_interfaceHandlers.find(eventType);
        if (it_ih != g_interfaceHandlers.end())
        {
            strong_handlers = it_ih->second.strongRefs;
            weak_handlers = it_ih->second.weakRefs;
        }

        auto it_cb = g_callbackHandlers.find(eventType);
        if (it_cb != g_callbackHandlers.end())
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

// =========================================================
// SyncEventCenter
// =========================================================

SyncEventCenter &SyncEventCenter::instance()
{
    static SyncEventCenter instance;
    return instance;
}

void SyncEventCenter::destroy()
{
    // Meyers Singleton handles destruction automatically.
}

// =========================================================
// AsyncEventCenter
// =========================================================

struct AsyncEventCenter::Impl
{
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

    std::vector<ScheduledEvent> m_pendingEvents;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent>> m_scheduledQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condVar;

    std::thread m_workerThread;
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_threadRunning{false};
    std::mutex m_threadMutex;

    Impl() = default;

    void ensureWorkerThread(AsyncEventCenter* parent)
    {
        if (!m_threadRunning)
        {
            std::lock_guard<std::mutex> lock(m_threadMutex);
            if (!m_threadRunning)
            {
                m_done = false;
                m_workerThread = std::thread(&Impl::processEvents, this);
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
                // Access base class method via scope resolution
                EventRegistry::dispatchEvent(scheduledEvent.eventData, scheduledEvent.eventType);
            }
        }
    }
};

AsyncEventCenter &AsyncEventCenter::instance()
{
    static AsyncEventCenter instance;
    return instance;
}

void AsyncEventCenter::destroy()
{
}

AsyncEventCenter::AsyncEventCenter() : m_impl(std::make_unique<Impl>())
{
}

AsyncEventCenter::~AsyncEventCenter()
{
    cancelAllEvents();
    m_impl->stopWorkerThread();
}

void AsyncEventCenter::cancelAllEvents()
{
    std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
    m_impl->m_pendingEvents.clear();
    m_impl->m_scheduledQueue = {};
}

void AsyncEventCenter::scheduleEvent(std::any event, std::type_index type, std::chrono::steady_clock::time_point timePoint)
{
    m_impl->ensureWorkerThread(this);
    Impl::ScheduledEvent newEvent{timePoint, event, type};
    {
        std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
        m_impl->m_pendingEvents.push_back(std::move(newEvent));
    }
    m_impl->m_condVar.notify_one();
}

} // namespace eventsystem