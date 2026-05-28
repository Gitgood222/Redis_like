#include "router.h"
#include "ds/list.h"
#include <cstdlib>

namespace redis {

namespace {

bool CheckExpire(CmdContext& ctx, const std::string& key) {
    bool expired = ctx.expire.LazyCheck(ctx.db, key, ctx.now);
    if (expired) ctx.RecordExpired();
    return expired;
}

// Get or create a List. Returns nullptr if wrong type.
std::shared_ptr<RedisObject> GetOrCreateList(CmdContext& ctx,
                                              const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) {
        obj = RedisObject::CreateList();
        ctx.db.Set(key, obj);
        return obj;
    }
    if (obj->type != ObjType::kList) return nullptr;
    return obj;
}

// Get an existing List. Returns nullptr if missing or wrong type.
std::shared_ptr<RedisObject> GetList(CmdContext& ctx,
                                      const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj || obj->type != ObjType::kList) { ctx.RecordMiss(); return nullptr; }
    ctx.RecordHit();
    return obj;
}

// Parse an integer arg at the given index. Returns nullopt on failure.
std::optional<int64_t> ParseInt(const std::string& s) {
    char* end = nullptr;
    int64_t v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

// ---------- command handlers ----------

// LPUSH key value [value ...]
std::string LPushCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'LPUSH'");

    auto obj = GetOrCreateList(ctx, ctx.cmd.args[0]);
    if (!obj)
        return RespCodec::Error("WRONGTYPE Operation against a key holding "
                                "the wrong kind of value");

    auto* lst = obj->As<RedisList>();
    for (size_t i = 1; i < ctx.cmd.args.size(); ++i)
        lst->push_front(ctx.cmd.args[i]);

    return RespCodec::Integer(static_cast<int64_t>(lst->size()));
}

// RPUSH key value [value ...]
std::string RPushCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'RPUSH'");

    auto obj = GetOrCreateList(ctx, ctx.cmd.args[0]);
    if (!obj)
        return RespCodec::Error("WRONGTYPE Operation against a key holding "
                                "the wrong kind of value");

    auto* lst = obj->As<RedisList>();
    for (size_t i = 1; i < ctx.cmd.args.size(); ++i)
        lst->push_back(ctx.cmd.args[i]);

    return RespCodec::Integer(static_cast<int64_t>(lst->size()));
}

// LPOP key [count]
std::string LPopCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'LPOP'");

    int64_t count = 1;
    if (ctx.cmd.args.size() >= 2) {
        auto v = ParseInt(ctx.cmd.args[1]);
        if (!v || *v < 0)
            return RespCodec::Error("ERR value is out of range");
        count = *v;
    }

    auto obj = GetList(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* lst = obj->As<RedisList>();
    std::vector<std::string> result;

    for (int64_t i = 0; i < count && !lst->empty(); ++i) {
        result.push_back(RespCodec::BulkString(lst->front()));
        lst->pop_front();
    }

    if (lst->empty()) ctx.db.Del(ctx.cmd.args[0]);

    if (ctx.cmd.args.size() >= 2)
        return RespCodec::Array(result);
    if (result.empty()) return RespCodec::NullBulkString();
    return result[0]; // single pop → BulkString
}

// RPOP key [count]
std::string RPopCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'RPOP'");

    int64_t count = 1;
    if (ctx.cmd.args.size() >= 2) {
        auto v = ParseInt(ctx.cmd.args[1]);
        if (!v || *v < 0)
            return RespCodec::Error("ERR value is out of range");
        count = *v;
    }

    auto obj = GetList(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* lst = obj->As<RedisList>();
    std::vector<std::string> result;

    for (int64_t i = 0; i < count && !lst->empty(); ++i) {
        result.push_back(RespCodec::BulkString(lst->back()));
        lst->pop_back();
    }

    if (lst->empty()) ctx.db.Del(ctx.cmd.args[0]);

    if (ctx.cmd.args.size() >= 2)
        return RespCodec::Array(result);
    if (result.empty()) return RespCodec::NullBulkString();
    return result[0];
}

// LLEN key
std::string LLenCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'LLEN'");

    auto obj = GetList(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* lst = obj->As<RedisList>();
    return RespCodec::Integer(static_cast<int64_t>(lst->size()));
}

// LRANGE key start stop
std::string LRangeCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 3)
        return RespCodec::Error("ERR wrong number of arguments for 'LRANGE'");

    auto start = ParseInt(ctx.cmd.args[1]);
    auto stop  = ParseInt(ctx.cmd.args[2]);
    if (!start || !stop)
        return RespCodec::Error("ERR value is not an integer");

    auto obj = GetList(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* lst = obj->As<RedisList>();

    // convert to List helper for Range
    redis::List helper;
    for (const auto& v : *lst)
        helper.RPush(v);

    auto items = helper.Range(*start, *stop);
    std::vector<std::string> resp;
    resp.reserve(items.size());
    for (const auto& item : items)
        resp.push_back(RespCodec::BulkString(item));
    return RespCodec::Array(resp);
}

// LINDEX key index
std::string LIndexCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'LINDEX'");

    auto idx = ParseInt(ctx.cmd.args[1]);
    if (!idx)
        return RespCodec::Error("ERR value is not an integer");

    auto obj = GetList(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* lst = obj->As<RedisList>();
    int64_t sz = static_cast<int64_t>(lst->size());
    if (sz == 0) return RespCodec::NullBulkString();

    // handle negative index
    if (*idx < 0) *idx += sz;
    if (*idx < 0 || *idx >= sz) return RespCodec::NullBulkString();

    auto it = lst->begin();
    std::advance(it, static_cast<size_t>(*idx));
    return RespCodec::BulkString(*it);
}

} // anonymous namespace

void RegisterListCommands(CommandRouter& r) {
    r.Register("LPUSH",  LPushCmd);
    r.Register("RPUSH",  RPushCmd);
    r.Register("LPOP",   LPopCmd);
    r.Register("RPOP",   RPopCmd);
    r.Register("LLEN",   LLenCmd);
    r.Register("LRANGE", LRangeCmd);
    r.Register("LINDEX", LIndexCmd);
}

}  // namespace redis
