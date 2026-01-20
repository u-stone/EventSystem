#include "EventSystem/EventCenter.h"
#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <thread>
#include <iostream>

namespace eventsystem {

// =========================================================
// EventRegistry Implementation (Hidden Static State)
// =========================================================

namespace {
    struct InterfaceHandlers
    {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };

    using GenericCallback = std::function<void(const std::any &)>;

    // Global Registry State
    std::map<std::type_index, InterfaceHandlers> g_interfaceHandlers;
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> g_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> g_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> g_nextSubscriptionId{1};
    std::mutex g_registryMutex;
}

void EventRegistry::RegisterInterfaceHandler(const std::type_index& type, const std::shared_ptr<IEventHandler>& handler, bool isWeak)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    if (isWeak) {
        g_interfaceHandlers[type].weakRefs.push_back(handler);
    } else {
        g_interfaceHandlers[type].strongRefs.push_back(handler);
    }
}

void EventRegistry::UnregisterInterfaceHandler(const std::type_index& type, const std::shared_ptr<IEventHandler>& handler)
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

SubscriptionHandle EventRegistry::RegisterCallbackHandler(const std::type_index& type, GenericCallback callback)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    SubscriptionHandle handle = g_nextSubscriptionId++;
    g_callbackHandlers[type][handle] = std::move(callback);
    g_handleToEventTypeMap.emplace(handle, type);
    return handle;
}

void EventRegistry::UnregisterHandler(SubscriptionHandle handle)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto itType = g_handleToEventTypeMap.find(handle);
    if (itType != g_handleToEventTypeMap.end())
    {
        std::type_index type = itType->second;
        auto itCbMap = g_callbackHandlers.find(type);
        if (itCbMap != g_callbackHandlers.end())
        {
            itCbMap->second.erase(handle);
            if (itCbMap->second.empty())
            {
                g_callbackHandlers.erase(itCbMap);
            }
        }
        g_handleToEventTypeMap.erase(itType);
    }
}

void EventRegistry::UnregisterAllHandlers(const std::type_index& type)
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

void EventRegistry::Reset()
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_interfaceHandlers.clear();
    g_callbackHandlers.clear();
    g_handleToEventTypeMap.clear();
    g_nextSubscriptionId = 1;
}

void EventRegistry::PrintSubscriptions()
{
    std::lock_guard<std::mutex> lock(g_registryMutex);
    std::cout << "--- EventCenter Subscriptions ---" << std::endl;
    
    if (g_interfaceHandlers.empty() && g_callbackHandlers.empty()) {
        std::cout << "  (No subscriptions)" << std::endl;
    } else {
        // Collect all types from both maps
        std::map<std::type_index, std::pair<size_t, size_t>> counts;
        for (const auto& [type, group] : g_interfaceHandlers) {
            counts[type].first = group.strongRefs.size() + group.weakRefs.size();
        }
        for (const auto& [type, cbMap] : g_callbackHandlers) {
            counts[type].second = cbMap.size();
        }

        for (const auto& [type, pair] : counts) {
            std::cout << "  Type: " << type.name() 
                      << " | Interface Handlers: " << pair.first
                      << " | Callback Handlers: " << pair.second << std::endl;
        }
    }
    std::cout << "---------------------------------" << std::endl;
}

void EventRegistry::DispatchEvent(const std::any &eventData, const std::type_index &eventType)
{
    std::vector<std::shared_ptr<IEventHandler>> strongHandlers;
    std::vector<std::shared_ptr<IEventHandler>> weakHandlersLocked;
    std::vector<GenericCallback> callbacks;

    {
        std::lock_guard<std::mutex> lock(g_registryMutex);

        auto it = g_interfaceHandlers.find(eventType);
        if (it != g_interfaceHandlers.end())
        {
            const auto &group = it->second;
            strongHandlers = group.strongRefs; 

            for (const auto &weak : group.weakRefs)
            {
                if (auto locked = weak.lock())
                {
                    weakHandlersLocked.push_back(locked);
                }
            }
        }

        auto itCb = g_callbackHandlers.find(eventType);
        if (itCb != g_callbackHandlers.end())
        {
            for (const auto &pair : itCb->second)
            {
                callbacks.push_back(pair.second);
            }
        }
    }

    for (const auto &handler : strongHandlers) { try { handler->Handle(eventData); } catch (...) {} }
    for (const auto &handler : weakHandlersLocked) { try { handler->Handle(eventData); } catch (...) {} }
    for (const auto &cb : callbacks) { try { cb(eventData); } catch (...) {} }
}

// =========================================================
// SyncEventCenter
// =========================================================

namespace {
    static SyncEventCenter* g_syncInstance = nullptr;
    static std::mutex g_syncCreationMutex;
}

SyncEventCenter& SyncEventCenter::Instance() {
    if (!g_syncInstance) {
        std::lock_guard<std::mutex> lock(g_syncCreationMutex);
        if (!g_syncInstance) {
            g_syncInstance = new SyncEventCenter();
        }
    }
    return *g_syncInstance;
}

void SyncEventCenter::Destroy() {
    std::lock_guard<std::mutex> lock(g_syncCreationMutex);
    if (g_syncInstance) {
        delete g_syncInstance;
        g_syncInstance = nullptr;
    }
}

// =========================================================
// AsyncEventCenter
// =========================================================

struct AsyncEventCenter::Impl {
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

    void WorkerLoop() {
        m_threadRunning = true;
        while (!m_done) {
            std::unique_lock<std::mutex> lock(m_queueMutex);

            if (m_scheduledQueue.empty() && m_pendingEvents.empty()) {
                m_condVar.wait(lock, [this] {
                    return m_done || !m_pendingEvents.empty() || !m_scheduledQueue.empty();
                });
            }

            if (m_done) break;

            if (!m_pendingEvents.empty()) {
                for (auto& evt : m_pendingEvents) {
                    m_scheduledQueue.push(std::move(evt));
                }
                m_pendingEvents.clear();
            }

            if (m_scheduledQueue.empty()) continue;

            auto now = std::chrono::steady_clock::now();
            const auto& top = m_scheduledQueue.top();

            if (top.executionTime <= now) {
                ScheduledEvent evt = std::move(const_cast<ScheduledEvent&>(top));
                m_scheduledQueue.pop();
                lock.unlock();
                EventRegistry::DispatchEvent(evt.eventData, evt.eventType);
            } else {
                m_condVar.wait_until(lock, top.executionTime);
            }
        }
        m_threadRunning = false;
    }
};

namespace {
    static AsyncEventCenter* g_asyncInstance = nullptr;
    static std::mutex g_asyncCreationMutex;
}

AsyncEventCenter& AsyncEventCenter::Instance() {
    if (!g_asyncInstance) {
        std::lock_guard<std::mutex> lock(g_asyncCreationMutex);
        if (!g_asyncInstance) {
            g_asyncInstance = new AsyncEventCenter();
        }
    }
    return *g_asyncInstance;
}

void AsyncEventCenter::Destroy() {
    std::lock_guard<std::mutex> lock(g_asyncCreationMutex);
    if (g_asyncInstance) {
        delete g_asyncInstance;
        g_asyncInstance = nullptr;
    }
}

AsyncEventCenter::AsyncEventCenter() : m_impl(new Impl()) {
    m_impl->m_workerThread = std::thread([this] { m_impl->WorkerLoop(); });
}

AsyncEventCenter::~AsyncEventCenter() {
    if (m_impl) {
        m_impl->m_done = true;
        m_impl->m_condVar.notify_all();
        if (m_impl->m_workerThread.joinable()) {
            m_impl->m_workerThread.join();
        }
        delete m_impl;
        m_impl = nullptr;
    }
}

void AsyncEventCenter::PublishEventInternal(const std::any& eventData, const std::type_index& type, const std::chrono::steady_clock::time_point& timePoint) {
    if (!m_impl) return;
    Impl::ScheduledEvent newEvent{timePoint, eventData, type};
    {
        std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
        m_impl->m_pendingEvents.push_back(std::move(newEvent));
    }
    m_impl->m_condVar.notify_one();
}

void AsyncEventCenter::CancelAllEvents() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
    while (!m_impl->m_scheduledQueue.empty()) {
        m_impl->m_scheduledQueue.pop();
    }
    m_impl->m_pendingEvents.clear();
}

} // namespace eventsystem
