#pragma once

#include "../common.h"
#include <list>

namespace redis {

// Redis List 的默认编码使用双向链表。
// Redis 也有 QuickList（ziplist + linkedlist 混合），这里先用 std::list 作为骨架。
//
// 后续可优化为自定义的链表实现以展示更多 C++ 功底。

using RedisLinkedList = std::list<std::string>;

class List {
public:
    // left push
    void LPush(const std::string& val) { list_.push_front(val); }

    // right push
    void RPush(const std::string& val) { list_.push_back(val); }

    // left pop
    std::optional<std::string> LPop() {
        if (list_.empty()) return std::nullopt;
        auto val = list_.front();
        list_.pop_front();
        return val;
    }

    // right pop
    std::optional<std::string> RPop() {
        if (list_.empty()) return std::nullopt;
        auto val = list_.back();
        list_.pop_back();
        return val;
    }

    // range [start, stop], supports negative indices
    std::vector<std::string> Range(int64_t start, int64_t stop) const;

    size_t Size()  const { return list_.size(); }
    bool   Empty() const { return list_.empty(); }

    auto begin() { return list_.begin(); }
    auto end()   { return list_.end(); }

private:
    RedisLinkedList list_;
};

}  // namespace redis
