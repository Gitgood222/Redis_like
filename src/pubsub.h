#pragma once

#include "event_loop.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace redis {

// Manages channel and pattern subscriptions for Pub/Sub.
class PubSubManager {
public:
    void Subscribe(socket_t fd, const std::string& channel);
    void Unsubscribe(socket_t fd, const std::string& channel);
    void PSubscribe(socket_t fd, const std::string& pattern);
    void PUnsubscribe(socket_t fd, const std::string& pattern);
    void UnsubscribeAll(socket_t fd);

    size_t SubscriptionCount(socket_t fd) const;
    size_t ChannelSubscriberCount(const std::string& channel) const;
    bool IsSubscribed(socket_t fd) const;

    // Return the channels / patterns a client is subscribed to.
    std::unordered_set<std::string> GetClientChannels(socket_t fd) const;
    std::unordered_set<std::string> GetClientPatterns(socket_t fd) const;

    // Deliver a message to all matching subscribers.  Returns the delivery
    // count and pushes messages via the supplied callback.
    using PushFn = std::function<void(socket_t fd, const std::string& channel,
                                      const std::string& message,
                                      const std::string& pattern)>;
    int Publish(const std::string& channel, const std::string& message,
                PushFn push);

private:
    static bool MatchGlob(const std::string& str, const std::string& pattern);

    // channel → set of fds
    std::unordered_map<std::string, std::unordered_set<socket_t>> channels_;
    // (pattern, set of fds)
    std::vector<std::pair<std::string, std::unordered_set<socket_t>>> patterns_;
    // fd → channels (for cleanup)
    std::unordered_map<socket_t, std::unordered_set<std::string>> client_channels_;
    // fd → patterns (for cleanup)
    std::unordered_map<socket_t, std::unordered_set<std::string>> client_patterns_;
};

}  // namespace redis
