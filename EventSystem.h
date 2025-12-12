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
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual void handle(const std::any& eventData) = 0;
};

//----------------------------------------------------------------
// The EventCenter is a singleton that manages and dispatches events asynchronously.
// The worker thread is created lazily on the first event post.
//----------------------------------------------------------------
class EventCenter {
public:
    // Provides access to the singleton instance.
    static EventCenter& instance() {
        static EventCenter center;
        return center;
    }

    // Deleted copy constructor and assignment operator.
    EventCenter(const EventCenter&) = delete;
    EventCenter& operator=(const EventCenter&) = delete;

    ~EventCenter() {
        m_done = true;
        m_condVar.notify_one();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    // Register a handler for a specific event type. Thread-safe.
    template<typename TEvent>
    void registerHandler(IEventHandler* handler) {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_mutex);
        m_handlers[eventType].push_back(handler);
        std::cout << "Handler registered for event: " << typeid(TEvent).name() << std::endl;
    }

    // Unregister a handler for a specific event type. Thread-safe.
    template<typename TEvent>
    void unregisterHandler(IEventHandler* handler) {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_handlers.find(eventType);
        if (it != m_handlers.end()) {
            auto& handlers = it->second;
            handlers.erase(std::remove(handlers.begin(), handlers.end(), handler), handlers.end());
            std::cout << "Handler unregistered for event: " << typeid(TEvent).name() << std::endl;
        }
    }

    // Post an event to the center. This is a non-blocking call.
    // The worker thread is created on the first call to this method.
    template<typename TEvent>
    void postEvent(const TEvent& event) {
        std::call_once(m_initFlag, &EventCenter::startWorker, this);

        std::type_index eventType = std::type_index(typeid(TEvent));

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(eventType);
            if (it != m_handlers.end()) {
                auto handlers = it->second; 
                m_eventQueue.push([handlers, event]() {
                    for (IEventHandler* handler : handlers) {
                        handler->handle(event);
                    }
                });
            }
        }
        m_condVar.notify_one();
    }

private:
    // Private constructor for singleton pattern.
    EventCenter() : m_done(false) {}

    void startWorker() {
        m_workerThread = std::thread(&EventCenter::processEvents, this);
    }

    void processEvents() {
        while (!m_done) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condVar.wait(lock, [this] { return !m_eventQueue.empty() || m_done; });

                if (m_done && m_eventQueue.empty()) {
                    return;
                }

                task = std::move(m_eventQueue.front());
                m_eventQueue.pop();
            }
            // Execute task outside the lock.
            task();
        }
    }

    std::map<std::type_index, std::vector<IEventHandler*>> m_handlers;
    std::queue<std::function<void()>> m_eventQueue;
    std::mutex m_mutex;
    std::condition_variable m_condVar;

    std::thread m_workerThread;
    std::atomic<bool> m_done;
    std::once_flag m_initFlag;
};

//----------------------------------------------------------------
// Helper "Tool" function to publish an event from anywhere.
// This provides a convenient fire-and-forget mechanism.
//----------------------------------------------------------------
template<typename TEvent>
void publish_event(const TEvent& event) {
    // Optional: add a log to know where event was published from.
    // std::cout << "Publishing event: " << typeid(TEvent).name() << std::endl;
    EventCenter::instance().postEvent(event);
}
