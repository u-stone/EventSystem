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

    // Main thread queue support
    std::queue<std::function<void()>> m_mainThreadQueue;
    std::mutex m_mainQueueMutex;
    std::atomic<double> m_maxUpdateMs{0.0};

    std::thread m_worker;
    std::once_flag m_workerStartedFlag;
    bool m_running{true};
    // Default to MainThread as per new requirement
    std::atomic<PublishMode> m_publishMode{PublishMode::MainThread};

    Impl() {
    }

    ~Impl() {
        Stop();
    }

    void EnsureWorkerStarted() {
        std::call_once(m_workerStartedFlag, [this]() {
            m_worker = std::thread(&Impl::WorkerLoop, this);
        });
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
                std::cout << "  Topic: " << kv.first 
                          << " | Subscribers: " << kv.second.size() << std::endl;
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

namespace {
    static std::atomic<MessageCenter*> g_instance{nullptr};
    
    // Prevent static initialization order fiasco
    std::mutex& GetCreationMutex() {
        static std::mutex m;
        return m;
    }
}

MessageCenter& MessageCenter::Instance() {
    MessageCenter* tmp = g_instance.load(std::memory_order_acquire);
    if (tmp == nullptr) {
        std::lock_guard<std::mutex> lock(GetCreationMutex());
        tmp = g_instance.load(std::memory_order_relaxed);
        if (tmp == nullptr) {
            tmp = new MessageCenter();
            g_instance.store(tmp, std::memory_order_release);
        }
    }
    return *tmp;
}

void MessageCenter::Destroy() {
    std::lock_guard<std::mutex> lock(GetCreationMutex());
    MessageCenter* tmp = g_instance.load(std::memory_order_relaxed);
    if (tmp) {
        delete tmp;
        g_instance.store(nullptr, std::memory_order_release);
    }
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
    return PublishMode::MainThread;
}

void MessageCenter::SetMaxUpdateDuration(double ms) {
    if (m_impl) {
        m_impl->m_maxUpdateMs.store(ms);
    }
}

void MessageCenter::Update() {
    if (!m_impl) return;

    auto start = std::chrono::steady_clock::now();
    double maxDuration = m_impl->m_maxUpdateMs.load();
    
    while (true) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(m_impl->m_mainQueueMutex);
            if (m_impl->m_mainThreadQueue.empty()) {
                return; // Queue empty, done
            }
            task = std::move(m_impl->m_mainThreadQueue.front());
            m_impl->m_mainThreadQueue.pop();
        }

        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[MessageCenter] Exception during MainThread update: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[MessageCenter] Unknown exception during MainThread update." << std::endl;
            }
        }

        // Check for timeout if a limit is set
        if (maxDuration > 0.0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double, std::milli>(now - start).count();
            if (elapsed >= maxDuration) {
                // Check if more tasks remain
                std::lock_guard<std::mutex> lock(m_impl->m_mainQueueMutex);
                if (!m_impl->m_mainThreadQueue.empty()) {
                    std::cerr << "[MessageCenter] Warning: Update time limit exceeded (" 
                              << elapsed << "ms >= " << maxDuration << "ms). "
                              << m_impl->m_mainThreadQueue.size() << " messages remaining." << std::endl;
                }
                break;
            }
        }
    }
}

MessageCenter::SubscriptionToken MessageCenter::SubscribeInternal(const std::string& topic, 
                                                                  std::any callback) {
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
    if (m_impl) {
        m_impl->EnsureWorkerStarted();
        {
            std::lock_guard<std::mutex> lock(m_impl->m_queueMutex);
            m_impl->m_queue.emplace(std::move(task));
        }
        m_impl->m_cv.notify_one();
    }
}

void MessageCenter::EnqueueMainThreadTask(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(m_impl->m_mainQueueMutex);
    m_impl->m_mainThreadQueue.emplace(std::move(task));
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
