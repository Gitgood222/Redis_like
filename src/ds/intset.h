#pragma once

#include "../common.h"
#include <cstdint>
#include <vector>

namespace redis {

// IntSet — packed sorted integer array, Redis's memory-efficient Set encoding.
// Elements are stored in ascending order in a compact uint8_t buffer.
// Encoding auto-upgrades: int16 → int32 → int64 as larger values are added.
class IntSet {
public:
    static constexpr uint8_t kInt16 = 2;
    static constexpr uint8_t kInt32 = 4;
    static constexpr uint8_t kInt64 = 8;

    void Add(int64_t val);
    bool Contains(int64_t val) const;
    void Remove(int64_t val);
    size_t Size() const { return size_; }
    std::vector<int64_t> GetAll() const;

private:
    // Binary search. Returns (index, found).
    std::pair<size_t, bool> Find(int64_t val) const;

    // Upgrade all elements to an encoding that can hold |val|.
    void UpgradeEncoding(int64_t val);

    // Raw pointer to element at index.
    uint8_t* At(size_t index);
    const uint8_t* At(size_t index) const;

    int64_t Get(size_t index) const;
    void Set(size_t index, int64_t val);
    void InsertAt(size_t index, int64_t val);
    void RemoveAt(size_t index);

    static uint8_t EncodeWidth(int64_t val);

    uint8_t encoding_ = kInt16;
    size_t size_ = 0;
    std::vector<uint8_t> data_;
};

}  // namespace redis
