#include "expire.h"
#include <random>
#include <algorithm>

namespace redis {

bool ExpireManager::LazyCheck(Dict& db, const std::string& key, TimePoint now) {
    auto obj = db.Get(key);
    if (!obj) return false;
    if (obj->IsExpired(now)) {
        db.Del(key);
        return true;
    }
    return false;
}

int ExpireManager::PeriodicCheck(Dict& db, TimePoint now, int sampleSize) {
    // Collect keys from the dict (copy — single-threaded, no concurrency issue)
    auto keys = db.Keys();
    if (keys.empty()) return 0;

    static thread_local std::mt19937 rng(std::random_device{}());

    int total_deleted = 0;
    int loop_count = 0;
    const int kMaxLoops = 16;  // safety limit

    do {
        int checked = 0;
        int deleted = 0;
        size_t remaining = keys.size();

        while (checked < sampleSize && remaining > 0) {
            std::uniform_int_distribution<size_t> dist(0, remaining - 1);
            size_t idx = dist(rng);
            const std::string& key = keys[idx];

            ++checked;

            auto obj = db.Get(key);
            bool expired = obj && obj->IsExpired(now);

            if (expired) {
                db.Del(key);
                ++deleted;
            }

            // Remove from sampling pool (swap with last)
            std::swap(keys[idx], keys[remaining - 1]);
            --remaining;
        }

        total_deleted += deleted;

        // Redis: if >25% sampled keys were expired, keep scanning
        if (deleted == 0 || (sampleSize > 0 && deleted * 4 <= checked)) {
            break;
        }

        // Remove the deleted/checked portion from the keys vector
        keys.resize(remaining);
        ++loop_count;
    } while (!keys.empty() && loop_count < kMaxLoops);

    return total_deleted;
}

void ExpireManager::SetExpire(std::shared_ptr<RedisObject> obj, TimePoint expireAt) {
    obj->expire_at = expireAt;
}

void ExpireManager::RemoveExpire(std::shared_ptr<RedisObject> obj) {
    obj->expire_at = std::nullopt;
}

int64_t ExpireManager::GetTTL(const std::shared_ptr<RedisObject>& obj, TimePoint now) const {
    if (!obj) return -2;                    // key not found
    if (!obj->expire_at) return -1;         // no expiry
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *obj->expire_at - now).count();
    return remaining >= 0 ? remaining : -2;  // expired
}

}  // namespace redis
