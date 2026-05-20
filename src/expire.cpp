#include "expire.h"

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
    // TODO: implement random sampling in stage 5
    (void)sampleSize;
    (void)now;
    (void)db;
    return 0;
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
