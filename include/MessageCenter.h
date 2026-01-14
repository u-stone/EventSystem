#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <thread>
#include <queue>
#include <condition_variable>

/**
 * @brief MessageRegistry manages subscriptions and dispatching logic for string-based events.
 * Base class for SyncMessageCenter and AsyncMessageCenter.
 */
class MessageRegistry {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using SubscriptionToken = size_t;

    virtual ~MessageRegistry() = default;

    /**
     * @brief Subscribes to a specific topic.
     * @param topic The topic string.
     * @param callback The callback function (supports Lambda, function pointers, functors).
     * @return SubscriptionToken A token used for later unsubscription.
     */
    SubscriptionToken subscribe(const std::string& topic, MessageCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        SubscriptionToken token = m_nextToken++;
        m_subscriptions[topic].push_back({token, std::move(callback)});
        return token;
    }

    /**
     * @brief Unsubscribes from a topic.
     * @param topic The topic string.
     * @param token The token returned during subscription.
     */
    void unsubscribe(const std::string& topic, SubscriptionToken token) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_subscriptions.find(topic);
        if (it != m_subscriptions.end()) {
            auto& subscribers = it->second;
            subscribers.erase(
                std::remove_if(subscribers.begin(), subscribers.end(),
                    [token](const SubscriberEntry& entry) { return entry.id == token; }),
                subscribers.end());
        }
    }

protected:
    void dispatch(const std::string& topic, const std::string& message) {
        std::vector<MessageCallback> callbacksToInvoke;
        
        // 1. Collect current callbacks within the lock to ensure thread safety and minimize lock contention.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_subscriptions.find(topic);
            if (it != m_subscriptions.end()) {
                for (const auto& entry : it->second) {
                    callbacksToInvoke.push_back(entry.callback);
                }
            }
        }

        // 2. Execute callbacks outside the lock to prevent deadlocks and allow re-entrancy 
        // (e.g., subscribing/unsubscribing inside a callback).
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

private:
    struct SubscriberEntry {
        SubscriptionToken id;
        MessageCallback callback;
    };

    std::unordered_map<std::string, std::vector<SubscriberEntry>> m_subscriptions;
    std::mutex m_mutex;
    std::atomic<SubscriptionToken> m_nextToken{0};
};

/**
 * @brief SyncMessageCenter dispatches messages immediately on the calling thread.
 */
class SyncMessageCenter : public MessageRegistry {
public:
    static SyncMessageCenter& instance() {
        static SyncMessageCenter inst;
        return inst;
    }

    /**
     * @brief Publishes a message immediately (Synchronous).
     */
    void publish(const std::string& topic, const std::string& message) {
        dispatch(topic, message);
    }

private:
    SyncMessageCenter() = default;
    ~SyncMessageCenter() = default;
    SyncMessageCenter(const SyncMessageCenter&) = delete;
    SyncMessageCenter& operator=(const SyncMessageCenter&) = delete;
};

/**
 * @brief AsyncMessageCenter dispatches messages using a background worker thread.
 */
class AsyncMessageCenter : public MessageRegistry {
public:
    static AsyncMessageCenter& instance() {
        static AsyncMessageCenter inst;
        return inst;
    }

    /**
     * @brief Publishes a message to the queue (Asynchronous).
     */
    void publish(const std::string& topic, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.push({topic, message});
        }
        m_cv.notify_one();
    }

    /**
     * @brief Stops the worker thread. Useful for clean shutdown.
     */
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

private:
    AsyncMessageCenter() {
        m_running = true;
        m_worker = std::thread(&AsyncMessageCenter::workerLoop, this);
    }

    ~AsyncMessageCenter() {
        stop();
    }

    AsyncMessageCenter(const AsyncMessageCenter&) = delete;
    AsyncMessageCenter& operator=(const AsyncMessageCenter&) = delete;

    struct MessageTask {
        std::string topic;
        std::string message;
    };

    void workerLoop() {
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

    std::queue<MessageTask> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running;
};

// Default alias to AsyncMessageCenter to match EventSystem's design pattern
using MessageCenter = AsyncMessageCenter;

//----------------------------------------------------------------
// Helper "Tool" functions for MessageCenter
//----------------------------------------------------------------

/**
 * @brief Publishes a message for immediate synchronous processing.
 * @param topic The topic string.
 * @param message The message content.
 */
inline void publish_message_sync(const std::string& topic, const std::string& message) {
    SyncMessageCenter::instance().publish(topic, message);
}

/**
 * @brief Publishes a message for asynchronous processing.
 * @param topic The topic string.
 * @param message The message content.
 */
inline void publish_message_async(const std::string& topic, const std::string& message) {
    AsyncMessageCenter::instance().publish(topic, message);
}

/**
 * @brief Publishes a message (Default: Asynchronous).
 * @param topic The topic string.
 * @param message The message content.
 */
inline void publish_message(const std::string& topic, const std::string& message) {
    publish_message_async(topic, message);
}

/**
 * @brief Subscribes to a topic on the AsyncMessageCenter.
 * @param topic The topic string.
 * @param callback The callback function.
 * @return SubscriptionToken used for unsubscription.
 */
inline MessageRegistry::SubscriptionToken subscribe_message_async(const std::string& topic, MessageRegistry::MessageCallback callback) {
    return AsyncMessageCenter::instance().subscribe(topic, std::move(callback));
}

/**
 * @brief Subscribes to a topic on the SyncMessageCenter.
 * @param topic The topic string.
 * @param callback The callback function.
 * @return SubscriptionToken used for unsubscription.
 */
inline MessageRegistry::SubscriptionToken subscribe_message_sync(const std::string& topic, MessageRegistry::MessageCallback callback) {
    return SyncMessageCenter::instance().subscribe(topic, std::move(callback));
}

/**
 * @brief Subscribes to a topic (Default: Asynchronous).
 * @param topic The topic string.
 * @param callback The callback function.
 * @return SubscriptionToken used for unsubscription.
 */
inline MessageRegistry::SubscriptionToken subscribe_message(const std::string& topic, MessageRegistry::MessageCallback callback) {
    return subscribe_message_async(topic, std::move(callback));
}

/**
 * @brief Unsubscribes from a topic on the AsyncMessageCenter.
 * @param topic The topic string.
 * @param token The token returned during subscription.
 */
inline void unsubscribe_message_async(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    AsyncMessageCenter::instance().unsubscribe(topic, token);
}

/**
 * @brief Unsubscribes from a topic on the SyncMessageCenter.
 * @param topic The topic string.
 * @param token The token returned during subscription.
 */
inline void unsubscribe_message_sync(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    SyncMessageCenter::instance().unsubscribe(topic, token);
}

/**
 * @brief Unsubscribes from a topic (Default: Asynchronous).
 * @param topic The topic string.
 * @param token The token returned during subscription.
 */
inline void unsubscribe_message(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    unsubscribe_message_async(topic, token);
}