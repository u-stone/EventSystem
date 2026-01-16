#include "EventSystem/MessageCenter.h"
#include <algorithm>

namespace eventsystem {

MessageCenter& MessageCenter::instance() {
    static MessageCenter inst;
    return inst;
}

MessageCenter::MessageCenter() {
    m_running = true;
    m_worker = std::thread(&MessageCenter::workerLoop, this);
}

MessageCenter::~MessageCenter() {
    stop();
}

void MessageCenter::stop() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void MessageCenter::unsubscribe(const std::string& topic, SubscriptionToken token) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto it = m_subscriptions.find(topic);
    if (it != m_subscriptions.end()) {
        auto& subscribers = it->second;
        subscribers.erase(
            std::remove_if(subscribers.begin(), subscribers.end(),
                [token](const SubscriberEntry& entry) { return entry.id == token; }),
            subscribers.end());
    }
}

void MessageCenter::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_subscriptions.erase(topic);
}

void MessageCenter::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });

            if (!m_running && m_queue.empty()) {
                return;
            }

            if (m_queue.empty()) continue;

            task = std::move(m_queue.front());
            m_queue.pop();
        }
        
        // Execute the generic task (which contains the bound dispatch call)
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[MessageCenter] Async worker exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[MessageCenter] Async worker unknown exception." << std::endl;
            }
        }
    }
}

} // namespace eventsystem