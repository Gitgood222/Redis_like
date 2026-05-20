#pragma once

#include "object.h"

namespace redis {

// 全局键空间 — 单线程，无锁
class Dict {
public:
    using Map = std::unordered_map<std::string, std::shared_ptr<RedisObject>>;

    std::shared_ptr<RedisObject> Get(std::string_view key) const {
        auto it = map_.find(std::string(key));
        if (it == map_.end()) return nullptr;
        return it->second;
    }

    void Set(const std::string& key, std::shared_ptr<RedisObject> obj) {
        map_[key] = std::move(obj);
    }

    bool Del(std::string_view key) {
        return map_.erase(std::string(key)) > 0;
    }

    bool Exists(std::string_view key) const {
        return map_.find(std::string(key)) != map_.end();
    }

    std::vector<std::string> Keys() const {
        std::vector<std::string> keys;
        keys.reserve(map_.size());
        for (const auto& kv : map_) keys.push_back(kv.first);
        return keys;
    }

    size_t Size() const { return map_.size(); }

    Map::iterator begin() { return map_.begin(); }
    Map::iterator end()   { return map_.end(); }
    Map::const_iterator begin() const { return map_.begin(); }
    Map::const_iterator end()   const { return map_.end(); }

private:
    Map map_;
};

}  // namespace redis
