#pragma once

#include "../common.h"
#include <cstdlib>
#include <random>
#include <vector>

namespace redis {

// ---------- 跳表节点 ----------
struct SkipListNode {
    double score;
    std::string member;
    std::vector<SkipListNode*> forward;   // forward[i] = next node at level i
    SkipListNode* backward = nullptr;     // back pointer (level 0 only)
    int span = 0;                         // span for rank calculation

    SkipListNode(double s, std::string m, int level)
        : score(s), member(std::move(m)), forward(level, nullptr) {}
};

// ---------- 跳表 ----------
// 用于 ZSet 实现，支持 ZADD / ZRANGE / ZRANK / ZSCORE 等操作。
// 采用与 Redis 类似的幂次随机层高算法。
class SkipList {
public:
    static constexpr int kMaxLevel = 32;

    SkipList();
    ~SkipList();

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    // 插入节点；若 member 已存在则更新 score。返回 true 表示新增。
    bool Insert(double score, const std::string& member);

    // 删除节点；返回 true 表示成功删除。
    bool Delete(const std::string& member);

    // 查找 member 的 score。返回 nullopt 表示不存在。
    std::optional<double> GetScore(const std::string& member) const;

    // 获取 member 的排名（0-based），从低到高。返回 nullopt 表示不存在。
    std::optional<int64_t> GetRank(const std::string& member) const;

    // 按排名范围获取成员 [start, stop]（0-based，含两端）。
    std::vector<std::string> RangeByRank(int64_t start, int64_t stop) const;

    // 按 score 范围获取成员。
    std::vector<std::string> RangeByScore(double min, double max) const;

    size_t Size() const { return size_; }

private:
    SkipListNode* header_ = nullptr;
    SkipListNode* tail_   = nullptr;
    int    level_ = 1;
    size_t size_  = 0;

    std::mt19937 rng_{std::random_device{}()};

    int RandomLevel();

    // member → score 辅助索引（O(1) 查询）
    std::unordered_map<std::string, double> score_map_;
};

}  // namespace redis
