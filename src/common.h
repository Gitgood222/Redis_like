#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <optional>
#include <chrono>
#include <variant>

namespace redis {

constexpr int kDefaultPort = 6379;
constexpr int kMaxClients = 1024;
constexpr int kReadBufferSize = 4096;
constexpr size_t kMaxBulkSize = 512 * 1024 * 1024;  // 512MB

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

enum class ObjType : uint8_t {
    kString = 0,
    kHash,
    kList,
    kSet,
    kZSet,
};

enum class ObjEncoding : uint8_t {
    kRaw = 0,
    kInt,
    kZipList,
    kSkipList,
    kHT,
};

inline const char* ObjTypeName(ObjType t) {
    switch (t) {
        case ObjType::kString: return "string";
        case ObjType::kHash:   return "hash";
        case ObjType::kList:   return "list";
        case ObjType::kSet:    return "set";
        case ObjType::kZSet:   return "zset";
    }
    return "unknown";
}

}  // namespace redis
