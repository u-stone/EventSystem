#include "EventSystem/EventCenter.h"

namespace eventsystem {

// =========================================================
// EventRegistry
// =========================================================

void EventRegistry::unregisterHandler(SubscriptionHandle handle)
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

void EventRegistry::dispatchEvent(const std::any &eventData, const std::type_index &eventType)
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

// =========================================================
// SyncEventCenter
// =========================================================

std::atomic<SyncEventCenter *> SyncEventCenter::m_instance{nullptr};
std::mutex SyncEventCenter::m_creationMutex;

SyncEventCenter &SyncEventCenter::instance()
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

void SyncEventCenter::destroy()
{
    std::lock_guard<std::mutex> lock(m_creationMutex);
    SyncEventCenter *ptr = m_instance.load(std::memory_order_acquire);
    if (ptr)
    {
        delete ptr;
        m_instance.store(nullptr, std::memory_order_release);
    }
}

// =========================================================
// AsyncEventCenter
// =========================================================

std::atomic<AsyncEventCenter *> AsyncEventCenter::m_instance{nullptr};
std::mutex AsyncEventCenter::m_creationMutex;

AsyncEventCenter &AsyncEventCenter::instance()
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

void AsyncEventCenter::destroy()
{
    std::lock_guard<std::mutex> lock(m_creationMutex);
    AsyncEventCenter *ptr = m_instance.load(std::memory_order_acquire);
    if (ptr)
    {
        delete ptr;
        m_instance.store(nullptr, std::memory_order_release);
    }
}

AsyncEventCenter::~AsyncEventCenter()
{
    cancelAllEvents();
    stopWorkerThread();
}

void AsyncEventCenter::cancelAllEvents()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_pendingEvents.clear();
    m_scheduledQueue = {};
}

void AsyncEventCenter::ensureWorkerThread()
{
    if (!m_threadRunning)
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (!m_threadRunning)
        {
            m_done = false;
            m_workerThread = std::thread(&AsyncEventCenter::processEvents, this);
            m_threadRunning = true;
        }
    }
}

void AsyncEventCenter::stopWorkerThread()
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

void AsyncEventCenter::processEvents()
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

} // namespace eventsystem
