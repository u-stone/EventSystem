#include "EventSystem/MessageCenter.h"
#include <algorithm>
#include <iostream>

namespace eventsystem {

// =========================================================
// MessageRegistry
// =========================================================

// Define static members
std::unordered_map<std::string, std::vector<MessageRegistry::SubscriberEntry>> MessageRegistry::m_subscriptions;
std::mutex MessageRegistry::m_registryMutex;
std::atomic<MessageRegistry::SubscriptionToken> MessageRegistry::m_nextToken{0};

MessageRegistry::SubscriptionToken MessageRegistry::subscribe(const std::string& topic, MessageCallback callback) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    SubscriptionToken token = m_nextToken++;
    m_subscriptions[topic].push_back({token, std::move(callback)});
    return token;
}

void MessageRegistry::unsubscribe(const std::string& topic, SubscriptionToken token) {
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

void MessageRegistry::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_subscriptions.erase(topic);
}

void MessageRegistry::dispatch(const std::string& topic, const std::string& message) {
    std::vector<MessageCallback> callbacksToInvoke;
    
    // 1. Collect current callbacks within the lock (Shared Data)
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        auto it = m_subscriptions.find(topic);
        if (it != m_subscriptions.end()) {
            for (const auto& entry : it->second) {
                callbacksToInvoke.push_back(entry.callback);
            }
        }
    }

    // 2. Execute callbacks outside the lock
    for (const auto& cb : callbacksToInvoke) {
        try {
            if (cb) {
                cb(message);
            }
        } catch (const std::exception& e) {
            std::cerr << "[MessageCenter] Exception in topic '" << topic << "': " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[MessageCenter] Unknown exception in topic '" << topic << "'" << std::endl;
        }
    }
}

// =========================================================
// SyncMessageCenter
// =========================================================

SyncMessageCenter& SyncMessageCenter::instance() {
    static SyncMessageCenter inst;
    return inst;
}

void SyncMessageCenter::publish(const std::string& topic, const std::string& message) {
    dispatch(topic, message);
}

// =========================================================
// AsyncMessageCenter
// =========================================================

AsyncMessageCenter& AsyncMessageCenter::instance() {
    static AsyncMessageCenter inst;
    return inst;
}

AsyncMessageCenter::AsyncMessageCenter() {
    m_running = true;
    m_worker = std::thread(&AsyncMessageCenter::workerLoop, this);
}

AsyncMessageCenter::~AsyncMessageCenter() {
    stop();
}

void AsyncMessageCenter::publish(const std::string& topic, const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push({topic, message});
    }
    m_cv.notify_one();
}

void AsyncMessageCenter::stop() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void AsyncMessageCenter::workerLoop() {
    while (true) {
        MessageTask task;
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
        dispatch(task.topic, task.message);
    }
}

} // namespace eventsystem
