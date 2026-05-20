#include "router.h"
#include "../ds/skiplist.h"
#include <cstdlib>
#include <sstream>

namespace redis {

namespace {

bool CheckExpire(CmdContext& ctx, const std::string& key) {
    return ctx.expire.LazyCheck(ctx.db, key, ctx.now);
}

// Get or create a ZSet. Returns nullptr if wrong type.
std::shared_ptr<RedisObject> GetOrCreateZSet(CmdContext& ctx,
                                              const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) {
        obj = RedisObject::CreateZSet();
        ctx.db.Set(key, obj);
        return obj;
    }
    if (obj->type != ObjType::kZSet) return nullptr;
    return obj;
}

// Get existing ZSet. Returns nullptr if missing or wrong type.
std::shared_ptr<RedisObject> GetZSet(CmdContext& ctx,
                                      const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj || obj->type != ObjType::kZSet) return nullptr;
    return obj;
}

// Get SkipList* from a ZSet object.
SkipList* GetSkipList(std::shared_ptr<RedisObject>& obj) {
    return obj->As<std::shared_ptr<SkipList>>()->get();
}

// Parse double. Returns nullopt on failure.
std::optional<double> ParseDouble(const std::string& s) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

std::optional<int64_t> ParseInt(const std::string& s) {
    char* end = nullptr;
    int64_t v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

// ---------- command handlers ----------

// ZADD key score member [score member ...]
std::string ZAddCmd(CmdContext& ctx) {
    auto& args = ctx.cmd.args;
    if (args.size() < 3 || (args.size() % 2) != 1)
        return RespCodec::Error("ERR wrong number of arguments for 'ZADD'");

    auto obj = GetOrCreateZSet(ctx, args[0]);
    if (!obj)
        return RespCodec::Error("WRONGTYPE Operation against a key holding "
                                "the wrong kind of value");

    auto* sl = GetSkipList(obj);
    int64_t added = 0;

    for (size_t i = 1; i < args.size(); i += 2) {
        auto score = ParseDouble(args[i]);
        if (!score)
            return RespCodec::Error("ERR value is not a valid float");
        bool existed = sl->GetScore(args[i + 1]).has_value();
        sl->Insert(*score, args[i + 1]);
        if (!existed) ++added;
    }

    return RespCodec::Integer(added);
}

// ZREM key member [member ...]
std::string ZRemCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'ZREM'");

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* sl = GetSkipList(obj);
    int64_t removed = 0;
    for (size_t i = 1; i < ctx.cmd.args.size(); ++i) {
        if (sl->Delete(ctx.cmd.args[i])) ++removed;
    }
    if (sl->Size() == 0) ctx.db.Del(ctx.cmd.args[0]);
    return RespCodec::Integer(removed);
}

// ZSCORE key member
std::string ZScoreCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'ZSCORE'");

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* sl = GetSkipList(obj);
    auto score = sl->GetScore(ctx.cmd.args[1]);
    if (!score) return RespCodec::NullBulkString();

    // Format score without trailing zeros
    std::ostringstream oss;
    oss << *score;
    return RespCodec::BulkString(oss.str());
}

// ZRANK key member
std::string ZRankCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'ZRANK'");

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* sl = GetSkipList(obj);
    auto rank = sl->GetRank(ctx.cmd.args[1]);
    if (!rank) return RespCodec::NullBulkString();
    return RespCodec::Integer(*rank);
}

// ZREVRANK key member  (rank from high to low)
std::string ZRevRankCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'ZREVRANK'");

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* sl = GetSkipList(obj);
    auto rank = sl->GetRank(ctx.cmd.args[1]);
    if (!rank) return RespCodec::NullBulkString();
    // rev rank = size - 1 - rank
    int64_t rev_rank = static_cast<int64_t>(sl->Size()) - 1 - *rank;
    return RespCodec::Integer(rev_rank);
}

// ZRANGE key start stop [WITHSCORES]
std::string ZRangeCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 3)
        return RespCodec::Error("ERR wrong number of arguments for 'ZRANGE'");

    auto start = ParseInt(ctx.cmd.args[1]);
    auto stop  = ParseInt(ctx.cmd.args[2]);
    if (!start || !stop)
        return RespCodec::Error("ERR value is not an integer");

    bool withscores = false;
    if (ctx.cmd.args.size() >= 4) {
        std::string opt = ctx.cmd.args[3];
        for (auto& c : opt) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (opt == "WITHSCORES") withscores = true;
    }

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* sl = GetSkipList(obj);
    auto members = sl->RangeByRank(*start, *stop);

    std::vector<std::string> items;
    for (const auto& m : members) {
        items.push_back(RespCodec::BulkString(m));
        if (withscores) {
            auto score = sl->GetScore(m);
            std::ostringstream oss;
            oss << *score;
            items.push_back(RespCodec::BulkString(oss.str()));
        }
    }
    return RespCodec::Array(items);
}

// ZREVRANGE key start stop [WITHSCORES]
std::string ZRevRangeCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 3)
        return RespCodec::Error("ERR wrong number of arguments for 'ZREVRANGE'");

    auto start = ParseInt(ctx.cmd.args[1]);
    auto stop  = ParseInt(ctx.cmd.args[2]);
    if (!start || !stop)
        return RespCodec::Error("ERR value is not an integer");

    bool withscores = false;
    if (ctx.cmd.args.size() >= 4) {
        std::string opt = ctx.cmd.args[3];
        for (auto& c : opt) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (opt == "WITHSCORES") withscores = true;
    }

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* sl = GetSkipList(obj);
    int64_t sz = static_cast<int64_t>(sl->Size());
    if (sz == 0) return RespCodec::EmptyArray();

    // Normalize negative indices (reverse space)
    int64_t rs = *start;
    int64_t re = *stop;
    if (rs < 0) rs += sz;
    if (re < 0) re += sz;
    if (rs < 0) rs = 0;
    if (re >= sz) re = sz - 1;
    if (rs > re) return RespCodec::EmptyArray();

    // Reverse to forward: rev_idx r → forward_idx (sz-1-r)
    // rev range [rs, re] → forward range [sz-1-re, sz-1-rs]
    int64_t fwd_start = sz - 1 - re;
    int64_t fwd_stop  = sz - 1 - rs;

    auto members = sl->RangeByRank(fwd_start, fwd_stop);

    // RangeByRank returns ascending; reverse for high-to-low
    std::reverse(members.begin(), members.end());

    std::vector<std::string> items;
    for (const auto& m : members) {
        items.push_back(RespCodec::BulkString(m));
        if (withscores) {
            auto score = sl->GetScore(m);
            std::ostringstream oss;
            oss << *score;
            items.push_back(RespCodec::BulkString(oss.str()));
        }
    }
    return RespCodec::Array(items);
}

// ZCARD key
std::string ZCardCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'ZCARD'");

    auto obj = GetZSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* sl = GetSkipList(obj);
    return RespCodec::Integer(static_cast<int64_t>(sl->Size()));
}

} // anonymous namespace

void RegisterZSetCommands(CommandRouter& r) {
    r.Register("ZADD",      ZAddCmd);
    r.Register("ZREM",      ZRemCmd);
    r.Register("ZSCORE",    ZScoreCmd);
    r.Register("ZRANK",     ZRankCmd);
    r.Register("ZREVRANK",  ZRevRankCmd);
    r.Register("ZRANGE",    ZRangeCmd);
    r.Register("ZREVRANGE", ZRevRangeCmd);
    r.Register("ZCARD",     ZCardCmd);
}

}  // namespace redis
