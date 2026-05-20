#pragma once

#include "../common.h"
#include <vector>
#include <cstdint>

namespace redis {

// IntSet — Redis's memory-efficient sorted integer set encoding.
// Used as an internal encoding for Sets when all members are integers.
// Placeholder for stage 4.
class IntSet {
public:
    void Add(int64_t val);
    bool Contains(int64_t val) const;
    void Remove(int64_t val);
    size_t Size() const { return data_.size(); }

private:
    std::vector<int64_t> data_;
};

}  // namespace redis
