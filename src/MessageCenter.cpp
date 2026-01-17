#include "EventSystem/MessageCenter.h"
#include <algorithm>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <iostream>

namespace eventsystem {

struct MessageCenter::Impl {
    struct SubscriberEntry {
        SubscriptionToken id;
        std::any callback; 
    };

    std::unordered_map<std::string, std::vector<SubscriberEntry>> m_subscriptions;
    std::mutex m_registryMutex;
    std::atomic<SubscriptionToken> m_nextToken{0};

    std::queue<std::function<void()>> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running;

    Impl() {
        m_running = true;
        m_worker = std::thread(&Impl::workerLoop, this);
    }

    ~Impl() {
        stop();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_running = false;
        }
        m_cv.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    void workerLoop() {
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
};

MessageCenter& MessageCenter::instance() {
    static MessageCenter inst;
    return inst;
}

MessageCenter::MessageCenter() : m_impl(std::make_unique<Impl>()) {
}

MessageCenter::~MessageCenter() {
}

MessageCenter::SubscriptionToken MessageCenter::subscribeImpl(const std::string& topic, std::any callback) {
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    SubscriptionToken token = m_impl->m_nextToken++;
    m_impl->m_subscriptions[topic].push_back({token, std::move(callback)});
    return token;
}

void MessageCenter::unsubscribe(const std::string& topic, SubscriptionToken token) {
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    auto it = m_impl->m_subscriptions.find(topic);
    if (it != m_impl->m_subscriptions.end()) {
        auto& subscribers = it->second;
        subscribers.erase(
            std::remove_if(subscribers.begin(), subscribers.end(),
                [token](const Impl::SubscriberEntry& entry) { return entry.id == token; }),
            subscribers.end());
    }
}

void MessageCenter::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    m_impl->m_subscriptions.erase(topic);
}

std::vector<std::any> MessageCenter::getSubscribers(const std::string& topic) {
    std::vector<std::any> callbacksToInvoke;
    {
        std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
        auto it = m_impl->m_subscriptions.find(topic);
        if (it != m_impl->m_subscriptions.end()) {
            for (const auto& entry : it->second) {
                callbacksToInvoke.push_back(entry.callback);
            }
        }
    }
    return callbacksToInvoke;
}

void MessageCenter::queueTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
        m_impl->m_queue.emplace(std::move(task));
    }
    m_impl->m_cv.notify_one();
}

} // namespace eventsystem
