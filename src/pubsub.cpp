#include "pubsub.h"

namespace redis {

// ---------- glob matching for PSUBSCRIBE ----------

bool PubSubManager::MatchGlob(const std::string& str, const std::string& pattern) {
    size_t si = 0, pi = 0;
    size_t starIdx = std::string::npos;
    size_t matchIdx = 0;

    while (si < str.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == str[si])) {
            ++si; ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starIdx = pi; matchIdx = si; ++pi;
        } else if (starIdx != std::string::npos) {
            pi = starIdx + 1; ++matchIdx; si = matchIdx;
        } else if (pi < pattern.size() && pattern[pi] == '[') {
            size_t close = pattern.find(']', pi);
            if (close == std::string::npos) {
                if (pattern[pi] != str[si]) return false;
                ++si; ++pi;
            } else {
                bool negate = (pi + 1 < pattern.size() && pattern[pi + 1] == '^');
                size_t cs = negate ? pi + 2 : pi + 1;
                bool matched = false;
                for (size_t ci = cs; ci < close; ++ci) {
                    if (ci + 2 < close && pattern[ci + 1] == '-') {
                        if (str[si] >= pattern[ci] && str[si] <= pattern[ci + 2]) {
                            matched = true; break;
                        }
                        ci += 2;
                    } else if (pattern[ci] == str[si]) {
                        matched = true; break;
                    }
                }
                if (negate) matched = !matched;
                if (!matched) {
                    if (starIdx != std::string::npos) {
                        pi = starIdx + 1; ++matchIdx; si = matchIdx; continue;
                    }
                    return false;
                }
                ++si; pi = close + 1;
            }
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

// ---------- subscribe / unsubscribe ----------

void PubSubManager::Subscribe(socket_t fd, const std::string& channel) {
    channels_[channel].insert(fd);
    client_channels_[fd].insert(channel);
}

void PubSubManager::Unsubscribe(socket_t fd, const std::string& channel) {
    auto it = channels_.find(channel);
    if (it != channels_.end()) {
        it->second.erase(fd);
        if (it->second.empty()) channels_.erase(it);
    }
    auto cit = client_channels_.find(fd);
    if (cit != client_channels_.end()) {
        cit->second.erase(channel);
        if (cit->second.empty()) client_channels_.erase(cit);
    }
}

void PubSubManager::PSubscribe(socket_t fd, const std::string& pattern) {
    // Find or create pattern entry
    for (auto& [pat, fds] : patterns_) {
        if (pat == pattern) { fds.insert(fd); goto done; }
    }
    patterns_.push_back({pattern, {fd}});
done:
    client_patterns_[fd].insert(pattern);
}

void PubSubManager::PUnsubscribe(socket_t fd, const std::string& pattern) {
    for (auto it = patterns_.begin(); it != patterns_.end(); ) {
        if (it->first == pattern) {
            it->second.erase(fd);
            if (it->second.empty()) it = patterns_.erase(it);
            else ++it;
        } else {
            ++it;
        }
    }
    auto pit = client_patterns_.find(fd);
    if (pit != client_patterns_.end()) {
        pit->second.erase(pattern);
        if (pit->second.empty()) client_patterns_.erase(pit);
    }
}

void PubSubManager::UnsubscribeAll(socket_t fd) {
    // Remove from channels
    auto cit = client_channels_.find(fd);
    if (cit != client_channels_.end()) {
        for (const auto& ch : cit->second) {
            auto it = channels_.find(ch);
            if (it != channels_.end()) {
                it->second.erase(fd);
                if (it->second.empty()) channels_.erase(it);
            }
        }
        client_channels_.erase(cit);
    }

    // Remove from patterns
    auto pit = client_patterns_.find(fd);
    if (pit != client_patterns_.end()) {
        for (const auto& pat : pit->second) {
            for (auto it = patterns_.begin(); it != patterns_.end(); ) {
                if (it->first == pat) {
                    it->second.erase(fd);
                    if (it->second.empty()) it = patterns_.erase(it);
                    else ++it;
                } else {
                    ++it;
                }
            }
        }
        client_patterns_.erase(pit);
    }
}

size_t PubSubManager::SubscriptionCount(socket_t fd) const {
    size_t count = 0;
    auto cit = client_channels_.find(fd);
    if (cit != client_channels_.end()) count += cit->second.size();
    auto pit = client_patterns_.find(fd);
    if (pit != client_patterns_.end()) count += pit->second.size();
    return count;
}

size_t PubSubManager::ChannelSubscriberCount(const std::string& channel) const {
    auto it = channels_.find(channel);
    return it != channels_.end() ? it->second.size() : 0;
}

bool PubSubManager::IsSubscribed(socket_t fd) const {
    return client_channels_.count(fd) || client_patterns_.count(fd);
}

std::unordered_set<std::string> PubSubManager::GetClientChannels(socket_t fd) const {
    auto it = client_channels_.find(fd);
    if (it == client_channels_.end()) return {};
    return it->second;
}

std::unordered_set<std::string> PubSubManager::GetClientPatterns(socket_t fd) const {
    auto it = client_patterns_.find(fd);
    if (it == client_patterns_.end()) return {};
    return it->second;
}

// ---------- publish ----------

int PubSubManager::Publish(const std::string& channel, const std::string& message,
                            PushFn push) {
    int count = 0;

    // Direct channel subscribers
    auto it = channels_.find(channel);
    if (it != channels_.end()) {
        for (socket_t fd : it->second) {
            push(fd, channel, message, "");
            count++;
        }
    }

    // Pattern subscribers
    for (const auto& [pattern, fds] : patterns_) {
        if (MatchGlob(channel, pattern)) {
            for (socket_t fd : fds) {
                push(fd, channel, message, pattern);
                count++;
            }
        }
    }

    return count;
}

}  // namespace redis
