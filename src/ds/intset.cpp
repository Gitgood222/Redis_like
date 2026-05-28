#include "intset.h"
#include <algorithm>
#include <cstring>

namespace redis {

// ---------- helpers ----------

uint8_t IntSet::EncodeWidth(int64_t val) {
    if (val >= INT16_MIN && val <= INT16_MAX) return kInt16;
    if (val >= INT32_MIN && val <= INT32_MAX) return kInt32;
    return kInt64;
}

uint8_t* IntSet::At(size_t index) {
    return data_.data() + index * encoding_;
}

const uint8_t* IntSet::At(size_t index) const {
    return data_.data() + index * encoding_;
}

int64_t IntSet::Get(size_t index) const {
    const uint8_t* p = At(index);
    switch (encoding_) {
        case kInt16: {
            int16_t v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<int64_t>(v);
        }
        case kInt32: {
            int32_t v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<int64_t>(v);
        }
        case kInt64: {
            int64_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
    }
    return 0;
}

void IntSet::Set(size_t index, int64_t val) {
    uint8_t* p = At(index);
    switch (encoding_) {
        case kInt16: {
            auto v = static_cast<int16_t>(val);
            std::memcpy(p, &v, sizeof(v));
            break;
        }
        case kInt32: {
            auto v = static_cast<int32_t>(val);
            std::memcpy(p, &v, sizeof(v));
            break;
        }
        case kInt64: {
            std::memcpy(p, &val, sizeof(val));
            break;
        }
    }
}

// ---------- binary search ----------

std::pair<size_t, bool> IntSet::Find(int64_t val) const {
    if (size_ == 0) return {0, false};

    // Narrow the search range to the encoding's bounds
    int64_t lo = Get(0);
    int64_t hi = Get(size_ - 1);

    if (val < lo) return {0, false};
    if (val > hi) return {size_, false};

    size_t left = 0, right = size_ - 1;
    while (left <= right) {
        size_t mid = left + (right - left) / 2;
        int64_t cur = Get(mid);
        if (cur < val) {
            left = mid + 1;
        } else if (cur > val) {
            if (mid == 0) break;
            right = mid - 1;
        } else {
            return {mid, true};
        }
    }
    return {left, false};
}

// ---------- insert / remove ----------

void IntSet::InsertAt(size_t index, int64_t val) {
    size_t old_bytes = size_ * encoding_;
    size_t new_bytes = (size_ + 1) * encoding_;
    data_.resize(new_bytes);

    // Shift elements after |index| right by one
    size_t shift_start = index * encoding_;
    size_t shift_bytes = old_bytes - shift_start;
    if (shift_bytes > 0) {
        uint8_t* dst = data_.data() + shift_start + encoding_;
        uint8_t* src = data_.data() + shift_start;
        std::memmove(dst, src, shift_bytes);
    }

    Set(index, val);
    size_++;
}

void IntSet::RemoveAt(size_t index) {
    size_t shift_start = (index + 1) * encoding_;
    size_t shift_bytes = size_ * encoding_ - shift_start;
    if (shift_bytes > 0) {
        uint8_t* dst = data_.data() + index * encoding_;
        uint8_t* src = data_.data() + shift_start;
        std::memmove(dst, src, shift_bytes);
    }
    size_--;
    data_.resize(size_ * encoding_);
}

void IntSet::Add(int64_t val) {
    uint8_t needed = EncodeWidth(val);
    if (needed > encoding_) {
        UpgradeEncoding(val);
    }

    auto [idx, found] = Find(val);
    if (!found) {
        InsertAt(idx, val);
    }
}

bool IntSet::Contains(int64_t val) const {
    // Quick check: if val doesn't fit current encoding, it can't be here
    if (EncodeWidth(val) > encoding_) return false;
    auto [idx, found] = Find(val);
    return found;
}

void IntSet::Remove(int64_t val) {
    if (EncodeWidth(val) > encoding_) return;
    auto [idx, found] = Find(val);
    if (found) {
        RemoveAt(idx);
    }
}

// ---------- encoding upgrade ----------

void IntSet::UpgradeEncoding(int64_t val) {
    uint8_t new_enc = EncodeWidth(val);
    uint8_t old_enc = encoding_;
    encoding_ = new_enc;

    size_t new_bytes = size_ * new_enc;
    std::vector<uint8_t> new_data(new_bytes);

    // Copy old elements, widening each
    for (size_t i = 0; i < size_; i++) {
        const uint8_t* old_p = data_.data() + i * old_enc;
        uint8_t* new_p = new_data.data() + i * new_enc;

        // old value in int64
        int64_t old_val = 0;
        switch (old_enc) {
            case kInt16: { int16_t v; std::memcpy(&v, old_p, sizeof(v)); old_val = v; break; }
            case kInt32: { int32_t v; std::memcpy(&v, old_p, sizeof(v)); old_val = v; break; }
            case kInt64: { std::memcpy(&old_val, old_p, sizeof(old_val)); break; }
        }

        switch (new_enc) {
            case kInt32: { auto v = static_cast<int32_t>(old_val); std::memcpy(new_p, &v, sizeof(v)); break; }
            case kInt64: { std::memcpy(new_p, &old_val, sizeof(old_val)); break; }
        }
    }

    data_ = std::move(new_data);
}

std::vector<int64_t> IntSet::GetAll() const {
    std::vector<int64_t> result;
    result.reserve(size_);
    for (size_t i = 0; i < size_; i++) {
        result.push_back(Get(i));
    }
    return result;
}

}  // namespace redis
