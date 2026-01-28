#include "EventSystem/MessageCenter.h"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>

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
    bool m_running{true};
    std::atomic<PublishMode> m_publishMode{PublishMode::Async};

    Impl() {
        m_worker = std::thread(&Impl::WorkerLoop, this);
    }

    ~Impl() {
        Stop();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_running = false;
        }
        m_cv.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    void PrintSubscriptions() {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        std::cout << "--- MessageCenter Subscriptions ---" << std::endl;
        if (m_subscriptions.empty()) {
            std::cout << "  (No subscriptions)" << std::endl;
        } else {
            for (const auto& kv : m_subscriptions) {
                std::cout << "  Topic: " << kv.first << " | Subscribers: " << kv.second.size() << std::endl;
            }
        }
        std::cout << "-----------------------------------" << std::endl;
    }

    void WorkerLoop() {
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
                } catch (...) {}
            }
        }
    }
};

MessageCenter& MessageCenter::Instance() {
    static MessageCenter inst;
    return inst;
}

MessageCenter::MessageCenter() : m_impl(new Impl()) {}

MessageCenter::~MessageCenter() {
    if (m_impl) {
        delete m_impl;
        m_impl = nullptr;
    }
}

void MessageCenter::SetPublishMode(PublishMode mode) {
    if (m_impl) {
        m_impl->m_publishMode.store(mode);
    }
}

PublishMode MessageCenter::GetPublishMode() const {
    if (m_impl) {
        return m_impl->m_publishMode.load();
    }
    return PublishMode::Async;
}

MessageCenter::SubscriptionToken MessageCenter::SubscribeInternal(const std::string& topic, std::any callback) {
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    SubscriptionToken token = m_impl->m_nextToken++;
    m_impl->m_subscriptions[topic].push_back({token, std::move(callback)});
    return token;
}

void MessageCenter::Unsubscribe(const std::string& topic, SubscriptionToken token) {
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

void MessageCenter::Unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
    m_impl->m_subscriptions.erase(topic);
}

void MessageCenter::PrintSubscriptions() {
    if (m_impl) {
        m_impl->PrintSubscriptions();
    }
}

void MessageCenter::EnqueueTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
        m_impl->m_queue.emplace(std::move(task));
    }
    m_impl->m_cv.notify_one();
}

std::vector<std::any> MessageCenter::GetCallbacksInternal(const std::string& topic) {
    std::vector<std::any> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_impl->m_registryMutex);
        auto it = m_impl->m_subscriptions.find(topic);
        if (it != m_impl->m_subscriptions.end()) {
            for (const auto& entry : it->second) {
                callbacks.push_back(entry.callback);
            }
        }
    }
    return callbacks;
}

} // namespace eventsystem
