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
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual void handle(const std::any& eventData) = 0;
};

//----------------------------------------------------------------
// The EventCenter is a singleton that manages and dispatches events asynchronously.
// It supports both class-based (IEventHandler) and function-based (callback) handlers.
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

    // --- IEventHandler-based Subscription (using smart pointers) ---

    // Registers a handler. The handler must be passed as a std::shared_ptr.
    template<typename TEvent>
    void registerHandler(const std::shared_ptr<IEventHandler>& handler) {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_mutex);
        m_interfaceHandlers[eventType].push_back(handler); // Store as weak_ptr
    }

    // Unregisters a handler.
    template<typename TEvent>
    void unregisterHandler(const std::shared_ptr<IEventHandler>& handler) {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_interfaceHandlers.find(eventType);
        if (it != m_interfaceHandlers.end()) {
            auto& handlers = it->second;
            // Erase the handler if it's the one we are looking for, or if it has expired.
            handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                [&handler](const std::weak_ptr<IEventHandler>& weak) {
                    return weak.expired() || weak.lock() == handler;
                }), handlers.end());
        }
    }


    // --- Callback-based Subscription ---

    template<typename TEvent>
    SubscriptionHandle registerHandler(std::function<void(const TEvent&)> callback) {
        std::type_index eventType = std::type_index(typeid(TEvent));
        SubscriptionHandle handle = m_nextSubscriptionId++;

        // Create a wrapper that casts std::any to the correct event type.
        auto wrapper = [callback](const std::any& eventData) {
            if (auto* event = std::any_cast<TEvent>(&eventData)) {
                callback(*event);
            }
        };

        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbackHandlers[eventType][handle] = wrapper;
        m_handleToEventTypeMap.emplace(handle, eventType); // For quick unregistering
        return handle;
    }

    void unregisterHandler(SubscriptionHandle handle) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_handleToEventTypeMap.find(handle);
        if (it != m_handleToEventTypeMap.end()) {
            std::type_index eventType = it->second;
            m_callbackHandlers[eventType].erase(handle);
            m_handleToEventTypeMap.erase(it);
        }
    }

    // Unregisters all handlers (both class-based and callback-based) for a specific event type.
    template<typename TEvent>
    void unregisterAllHandlers() {
        std::type_index eventType = std::type_index(typeid(TEvent));
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. Clear callback handlers for this event type
        auto it_cb = m_callbackHandlers.find(eventType);
        if (it_cb != m_callbackHandlers.end()) {
            // Also remove the corresponding entries from the reverse map
            for (const auto& [handle, func] : it_cb->second) {
                m_handleToEventTypeMap.erase(handle);
            }
            m_callbackHandlers.erase(it_cb);
        }

        // 2. Clear interface handlers for this event type
        m_interfaceHandlers.erase(eventType);
    }

    // --- Event Posting ---

    template<typename TEvent>
    void postEvent(const TEvent& event) {
        std::call_once(m_initFlag, &EventCenter::startWorker, this);
        std::type_index eventType = std::type_index(typeid(TEvent));
        
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            
            // 1. Queue tasks for IEventHandler subscribers
            if (m_interfaceHandlers.count(eventType)) {
                auto weak_handlers = m_interfaceHandlers.at(eventType); 
                m_eventQueue.push([weak_handlers, event]() {
                    for (const auto& weak_handler : weak_handlers) {
                        // Lock the weak_ptr to get a shared_ptr.
                        if (auto shared_handler = weak_handler.lock()) {
                            // If successful, call the handler.
                            shared_handler->handle(event);
                        }
                    }
                });
            }

            // 2. Queue tasks for callback subscribers
            if (m_callbackHandlers.count(eventType)) {
                auto& handlerMap = m_callbackHandlers.at(eventType);
                for(auto const& [handle, callback] : handlerMap) {
                    m_eventQueue.push([callback, event](){
                        callback(event);
                    });
                }
            }
        }
        m_condVar.notify_one();
    }

private:
    using GenericCallback = std::function<void(const std::any&)>;

    // Private constructor for singleton pattern.
    EventCenter() : m_done(false) {}

    void startWorker() {
        m_workerThread = std::thread(&EventCenter::processEvents, this);
    }

    void processEvents() {
        while (!m_done) {
            std::vector<std::function<void()>> tasks;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condVar.wait(lock, [this] { return !m_eventQueue.empty() || m_done; });

                if (m_done && m_eventQueue.empty()) {
                    return;
                }
                
                while(!m_eventQueue.empty()){
                    tasks.push_back(std::move(m_eventQueue.front()));
                    m_eventQueue.pop();
                }
            }
            
            // Execute tasks outside the lock.
            for(const auto& task : tasks){
                task();
            }
        }
    }

    // Storage for IEventHandler-based subscriptions (now using weak_ptr for safety)
    std::map<std::type_index, std::vector<std::weak_ptr<IEventHandler>>> m_interfaceHandlers;
    
    // Storage for callback-based subscriptions
    std::map<std::type_index, std::map<SubscriptionHandle, GenericCallback>> m_callbackHandlers;
    std::map<SubscriptionHandle, std::type_index> m_handleToEventTypeMap;
    std::atomic<SubscriptionHandle> m_nextSubscriptionId{0};

    std::queue<std::function<void()>> m_eventQueue;
    std::mutex m_mutex;
    std::condition_variable m_condVar;
    
    std::thread m_workerThread;
    std::atomic<bool> m_done;
    std::once_flag m_initFlag;
};

//----------------------------------------------------------------
// Helper "Tool" function to publish an event from anywhere.
//
// This is the primary way to send events into the system. It offers
// a "fire-and-forget" mechanism. The function call is non-blocking
// and returns immediately, while the event is queued for asynchronous
// processing by the EventCenter's worker thread.
//
// Usage:
//
//   struct PlayerScoreChangeEvent { int newScore; };
//
//   void updatePlayerScore(int score) {
//       // Simply call publish_event from anywhere in the code.
//       publish_event(PlayerScoreChangeEvent{score});
//   }
//
//----------------------------------------------------------------
template<typename TEvent>
void publish_event(const TEvent& event) {
    EventCenter::instance().postEvent(event);
}
