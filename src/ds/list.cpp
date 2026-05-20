#include "list.h"

namespace redis {

std::vector<std::string> List::Range(int64_t start, int64_t stop) const {
    int64_t sz = static_cast<int64_t>(list_.size());
    if (sz == 0) return {};

    // handle negative indices
    if (start < 0) start += sz;
    if (stop  < 0) stop  += sz;

    // clamp
    if (start < 0) start = 0;
    if (stop >= sz) stop = sz - 1;
    if (start > stop) return {};

    std::vector<std::string> result;
    auto it = list_.begin();
    std::advance(it, start);
    for (int64_t i = start; i <= stop; ++i, ++it) {
        result.push_back(*it);
    }
    return result;
}

}  // namespace redis
