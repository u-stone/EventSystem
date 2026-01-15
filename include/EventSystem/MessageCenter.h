#pragma once

#include "Export.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>

namespace eventsystem {

/**
 * @brief MessageRegistry manages the shared subscriptions for all message centers.
 * Subscriptions are shared across Sync and Async centers.
 */
class EVENTSYSTEM_API MessageRegistry {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using SubscriptionToken = size_t;

    virtual ~MessageRegistry() = default;

    // Static because subscriptions are shared
    static SubscriptionToken subscribe(const std::string& topic, MessageCallback callback);
    static void unsubscribe(const std::string& topic, SubscriptionToken token);
    static void unsubscribe(const std::string& topic);

protected:
    // Dispatch is instance-based (context), but accesses shared data
    static void dispatch(const std::string& topic, const std::string& message);

private:
    struct SubscriberEntry {
        SubscriptionToken id;
        MessageCallback callback;
    };

    // Shared state
    static std::unordered_map<std::string, std::vector<SubscriberEntry>> m_subscriptions;
    static std::mutex m_registryMutex;
    static std::atomic<SubscriptionToken> m_nextToken;
};

/**
 * @brief SyncMessageCenter dispatches messages immediately on the calling thread.
 */
class EVENTSYSTEM_API SyncMessageCenter : public MessageRegistry {
public:
    static SyncMessageCenter& instance();

    void publish(const std::string& topic, const std::string& message);

private:
    SyncMessageCenter() = default;
    ~SyncMessageCenter() = default;
    SyncMessageCenter(const SyncMessageCenter&) = delete;
    SyncMessageCenter& operator=(const SyncMessageCenter&) = delete;
};

/**
 * @brief AsyncMessageCenter dispatches messages using a background worker thread.
 */
class EVENTSYSTEM_API AsyncMessageCenter : public MessageRegistry {
public:
    static AsyncMessageCenter& instance();

    void publish(const std::string& topic, const std::string& message);
    void stop();

private:
    AsyncMessageCenter();
    ~AsyncMessageCenter();

    AsyncMessageCenter(const AsyncMessageCenter&) = delete;
    AsyncMessageCenter& operator=(const AsyncMessageCenter&) = delete;

    struct MessageTask {
        std::string topic;
        std::string message;
    };

    void workerLoop();

    std::queue<MessageTask> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running;
};

// Default alias
using MessageCenter = AsyncMessageCenter;

//----------------------------------------------------------------
// Helper "Tool" functions for MessageCenter
//----------------------------------------------------------------

// Publishing (Still distinguishes Sync vs Async behavior)

inline void publish_message_sync(const std::string& topic, const std::string& message) {
    SyncMessageCenter::instance().publish(topic, message);
}

inline void publish_message_async(const std::string& topic, const std::string& message) {
    AsyncMessageCenter::instance().publish(topic, message);
}

inline void publish_message(const std::string& topic, const std::string& message) {
    publish_message_async(topic, message);
}

// Subscription (Unified - No distinction needed)

inline MessageCenter::SubscriptionToken subscribe_message(const std::string& topic, MessageCenter::MessageCallback callback) {
    return MessageRegistry::subscribe(topic, std::move(callback));
}

inline void unsubscribe_message(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    MessageRegistry::unsubscribe(topic, token);
}

inline void unsubscribe_message(const std::string& topic) {
    MessageRegistry::unsubscribe(topic);
}

} // namespace eventsystem
