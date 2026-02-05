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
// EventCenter Implementation (PIMPL)
// =========================================================

struct EventCenter::Impl {
    struct InterfaceHandlers
    {
        std::vector<std::shared_ptr<IEventHandler>> strongRefs;
        std::vector<std::weak_ptr<IEventHandler>> weakRefs;
    };

    using GenericCallback = std::function<void(const std::any &)>;

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

    // Registry Storage
    std::map<std::type_index, InterfaceHandlers> m_interfaceHandlers;
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> m_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> m_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> m_nextSubscriptionId{1};
    std::mutex m_registryMutex;

    // Async Queue
    std::vector<ScheduledEvent> m_pendingEvents;
    using ScheduledQueue = std::priority_queue<ScheduledEvent, 
                                               std::vector<ScheduledEvent>, 
                                               std::greater<ScheduledEvent>>;
    ScheduledQueue m_scheduledQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condVar;

    std::thread m_workerThread;
    std::atomic<bool> m_done{false};
    std::atomic<PublishMode> m_publishMode{PublishMode::Async};

    Impl() {
        m_workerThread = std::thread(&Impl::WorkerLoop, this);
    }

    ~Impl() {
        m_done = true;
        m_condVar.notify_all();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    void WorkerLoop() {
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
                
                // Dispatch logic inside Impl context
                DispatchEventDirect(evt.eventData, evt.eventType);
            } else {
                m_condVar.wait_until(lock, top.executionTime);
            }
        }
    }

    void DispatchEventDirect(const std::any &eventData, const std::type_index &eventType)
    {
        std::vector<std::shared_ptr<IEventHandler>> strongHandlers;
        std::vector<std::shared_ptr<IEventHandler>> weakHandlersLocked;
        std::vector<GenericCallback> callbacks;

        {
            std::lock_guard<std::mutex> lock(m_registryMutex);

            auto it = m_interfaceHandlers.find(eventType);
            if (it != m_interfaceHandlers.end())
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

            auto itCb = m_callbackHandlers.find(eventType);
            if (itCb != m_callbackHandlers.end())
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
};

namespace {
    static std::atomic<EventCenter*> g_instance{nullptr};
    static std::mutex g_creationMutex;
}

EventCenter& EventCenter::Instance() {
    EventCenter* tmp = g_instance.load(std::memory_order_acquire);
    if (tmp == nullptr) {
        std::lock_guard<std::mutex> lock(g_creationMutex);
        tmp = g_instance.load(std::memory_order_relaxed);
        if (tmp == nullptr) {
            tmp = new EventCenter();
            g_instance.store(tmp, std::memory_order_release);
        }
    }
    return *tmp;
}

void EventCenter::Destroy() {
    std::lock_guard<std::mutex> lock(g_creationMutex);
    EventCenter* tmp = g_instance.load(std::memory_order_relaxed);
    if (tmp) {
        delete tmp;
        g_instance.store(nullptr, std::memory_order_release);
    }
}

EventCenter::EventCenter() : m_impl(new Impl()) {}

EventCenter::~EventCenter() {
    if (m_impl) {
        delete m_impl;
        m_impl = nullptr;
    }
}

void EventCenter::SetPublishMode(PublishMode mode) {
    if (m_impl) m_impl->m_publishMode.store(mode);
}

PublishMode EventCenter::GetPublishMode() const {
    if (m_impl) return m_impl->m_publishMode.load();
    return PublishMode::Async;
}

SubscriptionHandle EventCenter::SubscribeInternal(const std::type_index& type, 
                                                  const std::shared_ptr<IEventHandler>& handler, 
                                                  bool isWeak) {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    if (isWeak) {
        m_impl->m_interfaceHandlers[type].weakRefs.push_back(handler);
    } else {
        m_impl->m_interfaceHandlers[type].strongRefs.push_back(handler);
    }
    return 0; // Interface handlers don't use handle-based unsubscription in this design
}

SubscriptionHandle EventCenter::SubscribeCallbackInternal(const std::type_index& type, 
                                                          GenericCallback callback) {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    SubscriptionHandle handle = m_impl->m_nextSubscriptionId++;
    m_impl->m_callbackHandlers[type][handle] = std::move(callback);
    m_impl->m_handleToEventTypeMap.emplace(handle, type);
    return handle;
}

void EventCenter::Unsubscribe(SubscriptionHandle handle) {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    auto itType = m_impl->m_handleToEventTypeMap.find(handle);
    if (itType != m_impl->m_handleToEventTypeMap.end())
    {
        std::type_index type = itType->second;
        auto itCbMap = m_impl->m_callbackHandlers.find(type);
        if (itCbMap != m_impl->m_callbackHandlers.end())
        {
            itCbMap->second.erase(handle);
            if (itCbMap->second.empty()) m_impl->m_callbackHandlers.erase(itCbMap);
        }
        m_impl->m_handleToEventTypeMap.erase(itType);
    }
}

void EventCenter::UnsubscribeAllInternal(const std::type_index& type) {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    auto it_cb = m_impl->m_callbackHandlers.find(type);
    if (it_cb != m_impl->m_callbackHandlers.end())
    {
        for (const auto &[handle, func] : it_cb->second) m_impl->m_handleToEventTypeMap.erase(handle);
        m_impl->m_callbackHandlers.erase(it_cb);
    }
    m_impl->m_interfaceHandlers.erase(type);
}

void EventCenter::Reset() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    m_impl->m_interfaceHandlers.clear();
    m_impl->m_callbackHandlers.clear();
    m_impl->m_handleToEventTypeMap.clear();
    m_impl->m_nextSubscriptionId = 1;
}

void EventCenter::PrintSubscriptions() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    std::cout << "--- EventCenter Subscriptions ---" << std::endl;
    if (m_impl->m_interfaceHandlers.empty() && m_impl->m_callbackHandlers.empty()) {
        std::cout << "  (No subscriptions)" << std::endl;
    } else {
        std::map<std::type_index, std::pair<size_t, size_t>> counts;
        for (const auto& [type, group] : m_impl->m_interfaceHandlers) {
            counts[type].first = group.strongRefs.size() + group.weakRefs.size();
        }
        for (const auto& [type, cbMap] : m_impl->m_callbackHandlers) {
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

void EventCenter::DispatchEventInternal(const std::any &eventData, 
                                        const std::type_index &eventType) {
    if (m_impl) m_impl->DispatchEventDirect(eventData, eventType);
}

void EventCenter::EnqueueEventInternal(const std::any& eventData, 
                                       const std::type_index& type, 
                                       const std::chrono::steady_clock::time_point& timePoint) {
    if (!m_impl) return;
    Impl::ScheduledEvent newEvent{timePoint, eventData, type};
    {
        std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
        m_impl->m_pendingEvents.push_back(std::move(newEvent));
    }
    m_impl->m_condVar.notify_one();
}

void EventCenter::CancelAllEvents() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
    while (!m_impl->m_scheduledQueue.empty()) m_impl->m_scheduledQueue.pop();
    m_impl->m_pendingEvents.clear();
}

} // namespace eventsystem
