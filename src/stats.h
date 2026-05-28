#pragma once

#include "common.h"

namespace redis {

struct Stats {
    int64_t  total_commands_processed = 0;
    int64_t  total_connections_received = 0;
    int64_t  keyspace_hits = 0;
    int64_t  keyspace_misses = 0;
    int64_t  expired_keys = 0;
    TimePoint start_time = Clock::now();

    void RecordHit()     { keyspace_hits++; }
    void RecordMiss()    { keyspace_misses++; }
    void RecordExpired() { expired_keys++; }
};

}  // namespace redis
