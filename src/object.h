#pragma once

#include "common.h"

namespace redis {

// ---------- 基础数据单元 ----------
using RedisString = std::string;
using RedisHash   = std::unordered_map<std::string, std::string>;
using RedisList   = std::deque<std::string>;
using RedisSet    = std::unordered_set<std::string>;

// ZSet member: score + name
struct ZSetEntry {
    double score;
    std::string member;

    bool operator<(const ZSetEntry& o) const {
        if (score != o.score) return score < o.score;
        return member < o.member;
    }
};

// 前向声明跳表，避免循环依赖
struct SkipList;

// variant 类型
using ValueVariant = std::variant<
    RedisString,
    RedisHash,
    RedisList,
    RedisSet,
    std::shared_ptr<SkipList>  // ZSet → shared_ptr to avoid variant size bloat
>;

// ---------- RedisObject ----------
struct RedisObject {
    ObjType     type      = ObjType::kString;
    ObjEncoding encoding  = ObjEncoding::kRaw;
    ValueVariant value;
    std::optional<TimePoint> expire_at;  // nullopt = no expiry
    int64_t     lru       = 0;           // 访问计数

    // 工厂方法
    static std::shared_ptr<RedisObject> CreateString(std::string s = {}) {
        auto obj = std::make_shared<RedisObject>();
        obj->type = ObjType::kString;
        obj->encoding = ObjEncoding::kRaw;
        obj->value = RedisString{std::move(s)};
        return obj;
    }

    static std::shared_ptr<RedisObject> CreateHash() {
        auto obj = std::make_shared<RedisObject>();
        obj->type = ObjType::kHash;
        obj->encoding = ObjEncoding::kHT;
        obj->value = RedisHash{};
        return obj;
    }

    static std::shared_ptr<RedisObject> CreateList() {
        auto obj = std::make_shared<RedisObject>();
        obj->type = ObjType::kList;
        obj->encoding = ObjEncoding::kRaw;
        obj->value = RedisList{};
        return obj;
    }

    static std::shared_ptr<RedisObject> CreateSet() {
        auto obj = std::make_shared<RedisObject>();
        obj->type = ObjType::kSet;
        obj->encoding = ObjEncoding::kHT;
        obj->value = RedisSet{};
        return obj;
    }

    // ZSet factory — defined in ds/skiplist.cpp (needs full SkipList definition)
    static std::shared_ptr<RedisObject> CreateZSet();

    template <typename T>
    T* As() { return std::get_if<T>(&value); }

    template <typename T>
    const T* As() const { return std::get_if<T>(&value); }

    bool IsExpired(TimePoint now) const {
        return expire_at.has_value() && *expire_at <= now;
    }
};

}  // namespace redis
