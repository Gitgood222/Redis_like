#include "router.h"
#include <cstdlib>
#include <random>

namespace redis {

namespace {

bool CheckExpire(CmdContext& ctx, const std::string& key) {
    return ctx.expire.LazyCheck(ctx.db, key, ctx.now);
}

// Get or create a Set. Returns nullptr if wrong type.
std::shared_ptr<RedisObject> GetOrCreateSet(CmdContext& ctx,
                                             const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) {
        obj = RedisObject::CreateSet();
        ctx.db.Set(key, obj);
        return obj;
    }
    if (obj->type != ObjType::kSet) return nullptr;
    return obj;
}

// Get existing Set. Returns nullptr if missing or wrong type.
std::shared_ptr<RedisObject> GetSet(CmdContext& ctx,
                                     const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj || obj->type != ObjType::kSet) return nullptr;
    return obj;
}

// ---------- command handlers ----------

// SADD key member [member ...]
std::string SAddCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'SADD'");

    auto obj = GetOrCreateSet(ctx, ctx.cmd.args[0]);
    if (!obj)
        return RespCodec::Error("WRONGTYPE Operation against a key holding "
                                "the wrong kind of value");

    auto* set = obj->As<RedisSet>();
    int64_t added = 0;
    for (size_t i = 1; i < ctx.cmd.args.size(); ++i) {
        if (set->insert(ctx.cmd.args[i]).second) ++added;
    }
    return RespCodec::Integer(added);
}

// SREM key member [member ...]
std::string SRemCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'SREM'");

    CheckExpire(ctx, ctx.cmd.args[0]);
    auto obj = ctx.db.Get(ctx.cmd.args[0]);
    if (!obj || obj->type != ObjType::kSet) return RespCodec::Integer(0);

    auto* set = obj->As<RedisSet>();
    int64_t removed = 0;
    for (size_t i = 1; i < ctx.cmd.args.size(); ++i) {
        if (set->erase(ctx.cmd.args[i])) ++removed;
    }
    if (set->empty()) ctx.db.Del(ctx.cmd.args[0]);
    return RespCodec::Integer(removed);
}

// SISMEMBER key member
std::string SIsMemberCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'SISMEMBER'");

    auto obj = GetSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* set = obj->As<RedisSet>();
    return RespCodec::Integer(set->count(ctx.cmd.args[1]) ? 1 : 0);
}

// SMEMBERS key
std::string SMembersCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'SMEMBERS'");

    auto obj = GetSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* set = obj->As<RedisSet>();
    std::vector<std::string> items;
    items.reserve(set->size());
    for (const auto& m : *set)
        items.push_back(RespCodec::BulkString(m));
    return RespCodec::Array(items);
}

// SCARD key
std::string SCardCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'SCARD'");

    auto obj = GetSet(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* set = obj->As<RedisSet>();
    return RespCodec::Integer(static_cast<int64_t>(set->size()));
}

// SPOP key [count]
std::string SPopCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'SPOP'");

    int64_t count = 1;
    if (ctx.cmd.args.size() >= 2) {
        char* end = nullptr;
        count = std::strtoll(ctx.cmd.args[1].c_str(), &end, 10);
        if (end == ctx.cmd.args[1].c_str() || *end != '\0' || count < 0)
            return RespCodec::Error("ERR value is out of range");
    }

    auto obj = GetSet(ctx, ctx.cmd.args[0]);
    if (!obj) {
        if (ctx.cmd.args.size() >= 2) return RespCodec::EmptyArray();
        return RespCodec::NullBulkString();
    }

    auto* set = obj->As<RedisSet>();
    if (count > static_cast<int64_t>(set->size()))
        count = static_cast<int64_t>(set->size());

    std::vector<std::string> result;
    result.reserve(count);

    static thread_local std::mt19937 rng(std::random_device{}());

    for (int64_t i = 0; i < count; ++i) {
        auto it = set->begin();
        std::uniform_int_distribution<size_t> dist(0, set->size() - 1);
        std::advance(it, dist(rng));
        result.push_back(RespCodec::BulkString(*it));
        set->erase(it);
    }

    if (set->empty()) ctx.db.Del(ctx.cmd.args[0]);

    if (ctx.cmd.args.size() >= 2)
        return RespCodec::Array(result);
    if (result.empty()) return RespCodec::NullBulkString();
    return result[0];
}

} // anonymous namespace

void RegisterSetCommands(CommandRouter& r) {
    r.Register("SADD",      SAddCmd);
    r.Register("SREM",      SRemCmd);
    r.Register("SISMEMBER", SIsMemberCmd);
    r.Register("SMEMBERS",  SMembersCmd);
    r.Register("SCARD",     SCardCmd);
    r.Register("SPOP",      SPopCmd);
}

}  // namespace redis
