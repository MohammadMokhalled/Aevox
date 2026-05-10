#pragma once
// src/net/topic_bus.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// In-process publish/subscribe topic registry.
// One instance per App (stored in App::Impl). Not a global.
//
// Concurrency: std::shared_mutex protecting the topic map.
//   - publish(): shared lock to snapshot subscribers, then release lock before
//     calling session methods (prevents lock inversion deadlock).
//   - subscribe()/unsubscribe(): exclusive lock.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.5

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aevox::net {

// =============================================================================
// TopicSubscriber — abstract interface for TopicBus subscribers.
//
// WebSocketSession implements this interface. The test-mock also implements it,
// enabling TopicBus unit tests without Asio I/O.
// =============================================================================

class TopicSubscriber
{
public:
    TopicSubscriber()                                  = default;
    virtual ~TopicSubscriber()                         = default;
    TopicSubscriber(const TopicSubscriber&)            = delete;
    TopicSubscriber& operator=(const TopicSubscriber&) = delete;
    TopicSubscriber(TopicSubscriber&&)                 = delete;
    TopicSubscriber& operator=(TopicSubscriber&&)      = delete;

    /// Called by TopicBus::publish() to deliver a message to this subscriber.
    /// Thread-safe: must be safe to call from any thread.
    virtual void send_from_bus(std::string_view message) = 0;
};

// =============================================================================
// TopicBus
// =============================================================================

/**
 * @brief In-process pub/sub topic registry.
 *
 * Maps topic names to sets of TopicSubscriber instances. Used by
 * `WebSocket::subscribe()` and `WebSocket::publish()` to implement
 * room-based broadcast.
 *
 * Dead entries (expired weak_ptrs) are lazily pruned on each publish.
 *
 * @note Ownership: one instance per App, stored in App::Impl.
 * @note Thread-safety: `subscribe()`/`unsubscribe()` take an exclusive lock.
 *       `publish()` takes a shared lock to snapshot, releases the lock, then
 *       sends to each live subscriber outside the lock to prevent lock inversion.
 * @note Move semantics: not movable or copyable after construction.
 */
class TopicBus
{
public:
    TopicBus()  = default;
    ~TopicBus() = default;

    TopicBus(const TopicBus&)            = delete;
    TopicBus& operator=(const TopicBus&) = delete;
    TopicBus(TopicBus&&)                 = delete;
    TopicBus& operator=(TopicBus&&)      = delete;

    /**
     * @brief Subscribes `subscriber` to `topic`.
     *
     * If `subscriber` is already subscribed to `topic`, the call is idempotent
     * (the subscription is not duplicated).
     *
     * @param topic       Topic name.
     * @param subscriber  Subscriber to register. Stored as a weak_ptr — the bus
     *                    does not extend the subscriber's lifetime.
     */
    void subscribe(std::string_view topic, const std::shared_ptr<TopicSubscriber>& subscriber);

    /**
     * @brief Unsubscribes `subscriber` from all topics.
     *
     * Called when a session closes. Performs an exclusive write lock to remove
     * the subscriber's weak_ptr from every topic it was subscribed to.
     *
     * @param subscriber  Raw pointer used as identity key.
     */
    void unsubscribe(const TopicSubscriber* subscriber);

    /**
     * @brief Publishes `message` to all subscribers of `topic` except `sender`.
     *
     * Acquires a shared read lock to snapshot the subscriber list, releases
     * the lock, then calls `TopicSubscriber::send_from_bus()` on each live
     * subscriber. Dead weak_ptr entries are lazily removed (under exclusive
     * lock) after the snapshot walk.
     *
     * @param topic       Topic name.
     * @param message     UTF-8 message to deliver.
     * @param sender      The publishing subscriber; will not receive its own message.
     *                    Pass `nullptr` to deliver to all subscribers.
     * @return            Number of live delivery attempts made (excludes pruned
     *                    dead entries and the sender itself).
     */
    std::size_t publish(std::string_view topic, std::string_view message,
                        const TopicSubscriber* sender);

private:
    mutable std::shared_mutex                                                    mutex_;
    std::unordered_map<std::string, std::vector<std::weak_ptr<TopicSubscriber>>> topics_;
};

} // namespace aevox::net
