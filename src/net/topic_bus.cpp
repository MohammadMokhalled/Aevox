// src/net/topic_bus.cpp
//
// INTERNAL — TopicBus pub/sub topic registry implementation.
// No Asio types. Thread-safe via std::shared_mutex.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.5

#include "net/topic_bus.hpp"

#include <mutex>
#include <shared_mutex>
#include <vector>

namespace aevox::net {

// =============================================================================
// TopicBus::subscribe
// =============================================================================

void TopicBus::subscribe(std::string_view topic, const std::shared_ptr<TopicSubscriber>& subscriber)
{
    const std::string topic_key{topic};

    const std::unique_lock lock{mutex_};
    auto&                  vec = topics_[topic_key];

    // Check for existing subscription (idempotent).
    for (const auto& wp : vec) {
        if (auto sp = wp.lock(); sp && sp.get() == subscriber.get()) {
            return; // already subscribed
        }
    }
    vec.push_back(std::weak_ptr<TopicSubscriber>{subscriber});
}

// =============================================================================
// TopicBus::unsubscribe
// =============================================================================

void TopicBus::unsubscribe(const TopicSubscriber* subscriber)
{
    const std::unique_lock lock{mutex_};
    for (auto& [topic_key, vec] : topics_) {
        std::erase_if(vec, [subscriber](const std::weak_ptr<TopicSubscriber>& wp) {
            auto sp = wp.lock();
            return !sp || sp.get() == subscriber;
        });
    }
}

// =============================================================================
// TopicBus::publish
// =============================================================================

std::size_t TopicBus::publish(std::string_view topic, std::string_view message,
                              const TopicSubscriber* sender)
{
    const std::string topic_key{topic};

    // Snapshot the subscriber list under a shared (read) lock.
    std::vector<std::shared_ptr<TopicSubscriber>> live_subs;
    bool                                          has_dead = false;

    {
        const std::shared_lock lock{mutex_};
        auto                   it = topics_.find(topic_key);
        if (it == topics_.end())
            return 0;

        for (const auto& wp : it->second) {
            if (auto sp = wp.lock()) {
                live_subs.push_back(std::move(sp));
            }
            else {
                has_dead = true;
            }
        }
    }

    // Remove dead entries under exclusive lock (lazy GC).
    if (has_dead) {
        const std::unique_lock lock{mutex_};
        auto                   it = topics_.find(topic_key);
        if (it != topics_.end()) {
            std::erase_if(it->second,
                          [](const std::weak_ptr<TopicSubscriber>& wp) { return wp.expired(); });
        }
    }

    // Deliver to each live subscriber (outside any lock — prevents lock inversion).
    std::size_t deliveries = 0;
    for (const auto& sp : live_subs) {
        if (sp.get() == sender)
            continue; // suppress self-publish
        sp->send_from_bus(message);
        ++deliveries;
    }

    return deliveries;
}

} // namespace aevox::net
