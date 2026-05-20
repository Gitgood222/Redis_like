#include "skiplist.h"
#include "../object.h"
#include <algorithm>

namespace redis {

// RedisObject factory for ZSet type
std::shared_ptr<RedisObject> RedisObject::CreateZSet() {
    auto obj = std::make_shared<RedisObject>();
    obj->type = ObjType::kZSet;
    obj->encoding = ObjEncoding::kSkipList;
    obj->value = std::make_shared<SkipList>();
    return obj;
}

SkipList::SkipList() {
    header_ = new SkipListNode(0.0, "", kMaxLevel);
}

SkipList::~SkipList() {
    SkipListNode* node = header_;
    while (node) {
        SkipListNode* next = node->forward[0];
        delete node;
        node = next;
    }
}

int SkipList::RandomLevel() {
    // 幂次分布：每层概率 p=0.25
    int level = 1;
    while (level < kMaxLevel && (rng_() & 3) == 0) {
        ++level;
    }
    return level;
}

bool SkipList::Insert(double score, const std::string& member) {
    // Check if member already exists (update score)
    auto it = score_map_.find(member);
    if (it != score_map_.end()) {
        if (it->second == score) return false; // no change
        // Remove and re-insert
        Delete(member);
    }

    std::vector<SkipListNode*> update(kMaxLevel, nullptr);
    std::vector<int> rank(kMaxLevel, 0);

    SkipListNode* node = header_;
    for (int i = level_ - 1; i >= 0; --i) {
        rank[i] = (i == level_ - 1) ? 0 : rank[i + 1];
        while (node->forward[i] && node->forward[i]->score < score) {
            rank[i] += node->forward[i]->span;
            node = node->forward[i];
        }
        // tie-breaking by member
        while (node->forward[i] && node->forward[i]->score == score
               && node->forward[i]->member < member) {
            rank[i] += node->forward[i]->span;
            node = node->forward[i];
        }
        update[i] = node;
    }

    int level = RandomLevel();
    if (level > level_) {
        for (int i = level_; i < level; ++i) {
            rank[i] = 0;
            update[i] = header_;
            update[i]->span = static_cast<int>(size_);
        }
        level_ = level;
    }

    auto* newNode = new SkipListNode(score, member, level);
    for (int i = 0; i < level; ++i) {
        newNode->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = newNode;
    }

    newNode->backward = (update[0] == header_) ? nullptr : update[0];
    if (newNode->forward[0]) {
        newNode->forward[0]->backward = newNode;
    } else {
        tail_ = newNode;
    }

    score_map_[member] = score;
    ++size_;
    return true;
}

bool SkipList::Delete(const std::string& member) {
    auto it = score_map_.find(member);
    if (it == score_map_.end()) return false;

    double score = it->second;
    score_map_.erase(it);

    std::vector<SkipListNode*> update(kMaxLevel, nullptr);
    SkipListNode* node = header_;

    for (int i = level_ - 1; i >= 0; --i) {
        while (node->forward[i] && node->forward[i]->score < score) {
            node = node->forward[i];
        }
        while (node->forward[i] && node->forward[i]->score == score
               && node->forward[i]->member < member) {
            node = node->forward[i];
        }
        update[i] = node;
    }

    node = node->forward[0];
    if (!node || node->member != member) return false;

    for (int i = 0; i < level_; ++i) {
        if (update[i]->forward[i] == node) {
            update[i]->forward[i] = node->forward[i];
        }
    }

    if (node->forward[0]) {
        node->forward[0]->backward = node->backward;
    } else {
        tail_ = node->backward;
    }

    while (level_ > 1 && header_->forward[level_ - 1] == nullptr) {
        --level_;
    }

    delete node;
    --size_;
    return true;
}

std::optional<double> SkipList::GetScore(const std::string& member) const {
    auto it = score_map_.find(member);
    if (it == score_map_.end()) return std::nullopt;
    return it->second;
}

std::optional<int64_t> SkipList::GetRank(const std::string& member) const {
    auto it = score_map_.find(member);
    if (it == score_map_.end()) return std::nullopt;

    double score = it->second;
    int64_t rank = 0;
    SkipListNode* node = header_->forward[0];

    while (node) {
        if (node->score < score || (node->score == score && node->member < member)) {
            ++rank;
        } else if (node->member == member) {
            return rank;
        }
        node = node->forward[0];
    }

    return std::nullopt;
}

std::vector<std::string> SkipList::RangeByRank(int64_t start, int64_t stop) const {
    std::vector<std::string> result;
    if (start < 0) start = static_cast<int64_t>(size_) + start;
    if (stop  < 0) stop  = static_cast<int64_t>(size_) + stop;
    if (start < 0) start = 0;
    if (stop < 0) return result;
    if (start > stop) return result;

    SkipListNode* node = header_->forward[0];
    int64_t idx = 0;

    while (node && idx < start) {
        node = node->forward[0];
        ++idx;
    }

    while (node && idx <= stop) {
        result.push_back(node->member);
        node = node->forward[0];
        ++idx;
    }

    return result;
}

std::vector<std::string> SkipList::RangeByScore(double min, double max) const {
    std::vector<std::string> result;
    SkipListNode* node = header_->forward[0];

    while (node && node->score < min) {
        node = node->forward[0];
    }

    while (node && node->score <= max) {
        result.push_back(node->member);
        node = node->forward[0];
    }

    return result;
}

}  // namespace redis
