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
 * @brief MessageRegistry manages subscriptions and dispatching logic for string-based events.
 * Base class for SyncMessageCenter and AsyncMessageCenter.
 */
class EVENTSYSTEM_API MessageRegistry {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using SubscriptionToken = size_t;

    virtual ~MessageRegistry() = default;

    SubscriptionToken subscribe(const std::string& topic, MessageCallback callback);
    void unsubscribe(const std::string& topic, SubscriptionToken token);

protected:
    void dispatch(const std::string& topic, const std::string& message);

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

using MessageCenter = AsyncMessageCenter;

//----------------------------------------------------------------
// Helper "Tool" functions for MessageCenter (Inline for ease of use)
//----------------------------------------------------------------

inline void publish_message_sync(const std::string& topic, const std::string& message) {
    SyncMessageCenter::instance().publish(topic, message);
}

inline void publish_message_async(const std::string& topic, const std::string& message) {
    AsyncMessageCenter::instance().publish(topic, message);
}

inline void publish_message(const std::string& topic, const std::string& message) {
    publish_message_async(topic, message);
}

inline MessageRegistry::SubscriptionToken subscribe_message_async(const std::string& topic, MessageRegistry::MessageCallback callback) {
    return AsyncMessageCenter::instance().subscribe(topic, std::move(callback));
}

inline MessageRegistry::SubscriptionToken subscribe_message_sync(const std::string& topic, MessageRegistry::MessageCallback callback) {
    return SyncMessageCenter::instance().subscribe(topic, std::move(callback));
}

inline MessageRegistry::SubscriptionToken subscribe_message(const std::string& topic, MessageRegistry::MessageCallback callback) {
    return subscribe_message_async(topic, std::move(callback));
}

inline void unsubscribe_message_async(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    AsyncMessageCenter::instance().unsubscribe(topic, token);
}

inline void unsubscribe_message_sync(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    SyncMessageCenter::instance().unsubscribe(topic, token);
}

inline void unsubscribe_message(const std::string& topic, MessageRegistry::SubscriptionToken token) {
    unsubscribe_message_async(topic, token);
}

} // namespace eventsystem

