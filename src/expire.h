#pragma once

#include "common.h"
#include "object.h"
#include "dict.h"

namespace redis {

// ---------- 过期管理器 ----------
// 惰性删除 + 定期删除，参考 Redis 实现。
class ExpireManager {
public:
    // 惰性删除：访问 key 时检查是否过期，过期则删除并返回 true。
    bool LazyCheck(Dict& db, const std::string& key, TimePoint now);

    // 定期删除：随机抽样检查，每轮检查 sample_size 个带过期时间的 key。
    // 返回本轮删除的 key 数量。
    int PeriodicCheck(Dict& db, TimePoint now, int sampleSize = 20);

    // 设置 key 的过期时间。
    void SetExpire(std::shared_ptr<RedisObject> obj, TimePoint expireAt);

    // 移除过期时间（PERSIST）。
    void RemoveExpire(std::shared_ptr<RedisObject> obj);

    // 获取剩余 TTL（秒）。key 不存在返回 -2，无过期返回 -1。
    int64_t GetTTL(const std::shared_ptr<RedisObject>& obj, TimePoint now) const;
};

}  // namespace redis
