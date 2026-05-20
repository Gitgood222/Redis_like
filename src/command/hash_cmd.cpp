#include "router.h"
#include <cstdlib>
#include <algorithm>

namespace redis {

namespace {

// Check and perform lazy expiry on a key. Returns true if expired/deleted.
bool CheckExpire(CmdContext& ctx, const std::string& key) {
    return ctx.expire.LazyCheck(ctx.db, key, ctx.now);
}

// Get or create a Hash object for the given key.
// Returns nullptr if the key exists but is not a Hash.
std::shared_ptr<RedisObject> GetOrCreateHash(CmdContext& ctx,
                                              const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) {
        obj = RedisObject::CreateHash();
        ctx.db.Set(key, obj);
        return obj;
    }
    if (obj->type != ObjType::kHash) return nullptr;
    return obj;
}

// Get an existing Hash object. Returns nullptr if missing or wrong type.
std::shared_ptr<RedisObject> GetHash(CmdContext& ctx,
                                      const std::string& key) {
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj || obj->type != ObjType::kHash) return nullptr;
    return obj;
}

// ---------- command handlers ----------

// HSET key field value [field value ...]
std::string HSetCmd(CmdContext& ctx) {
    auto& args = ctx.cmd.args;
    if (args.size() < 3 || (args.size() % 2) != 1) {
        return RespCodec::Error("ERR wrong number of arguments for 'HSET'");
    }

    auto obj = GetOrCreateHash(ctx, args[0]);
    if (!obj)
        return RespCodec::Error("WRONGTYPE Operation against a key holding "
                                "the wrong kind of value");

    auto* hash = obj->As<RedisHash>();
    int64_t added = 0;
    for (size_t i = 1; i < args.size(); i += 2) {
        if (hash->find(args[i]) == hash->end()) ++added;
        (*hash)[args[i]] = args[i + 1];
    }
    return RespCodec::Integer(added);
}

// HGET key field
std::string HGetCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'HGET'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::NullBulkString();

    auto* hash = obj->As<RedisHash>();
    auto it = hash->find(ctx.cmd.args[1]);
    if (it == hash->end()) return RespCodec::NullBulkString();
    return RespCodec::BulkString(it->second);
}

// HDEL key field [field ...]
std::string HDelCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'HDEL'");

    CheckExpire(ctx, ctx.cmd.args[0]);
    auto obj = ctx.db.Get(ctx.cmd.args[0]);
    if (!obj || obj->type != ObjType::kHash) return RespCodec::Integer(0);

    auto* hash = obj->As<RedisHash>();
    int64_t count = 0;
    for (size_t i = 1; i < ctx.cmd.args.size(); ++i) {
        if (hash->erase(ctx.cmd.args[i])) ++count;
    }
    // Clean up empty hash
    if (hash->empty()) ctx.db.Del(ctx.cmd.args[0]);
    return RespCodec::Integer(count);
}

// HEXISTS key field
std::string HExistsCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'HEXISTS'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* hash = obj->As<RedisHash>();
    return RespCodec::Integer(hash->count(ctx.cmd.args[1]) ? 1 : 0);
}

// HKEYS key
std::string HKeysCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'HKEYS'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* hash = obj->As<RedisHash>();
    std::vector<std::string> items;
    items.reserve(hash->size());
    for (const auto& kv : *hash)
        items.push_back(RespCodec::BulkString(kv.first));
    return RespCodec::Array(items);
}

// HVALS key
std::string HValsCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'HVALS'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* hash = obj->As<RedisHash>();
    std::vector<std::string> items;
    items.reserve(hash->size());
    for (const auto& kv : *hash)
        items.push_back(RespCodec::BulkString(kv.second));
    return RespCodec::Array(items);
}

// HGETALL key
std::string HGetAllCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'HGETALL'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::EmptyArray();

    auto* hash = obj->As<RedisHash>();
    std::vector<std::string> items;
    items.reserve(hash->size() * 2);
    for (const auto& kv : *hash) {
        items.push_back(RespCodec::BulkString(kv.first));
        items.push_back(RespCodec::BulkString(kv.second));
    }
    return RespCodec::Array(items);
}

// HLEN key
std::string HLenCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments for 'HLEN'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* hash = obj->As<RedisHash>();
    return RespCodec::Integer(static_cast<int64_t>(hash->size()));
}

// HSTRLEN key field
std::string HStrLenCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2)
        return RespCodec::Error("ERR wrong number of arguments for 'HSTRLEN'");

    auto obj = GetHash(ctx, ctx.cmd.args[0]);
    if (!obj) return RespCodec::Integer(0);

    auto* hash = obj->As<RedisHash>();
    auto it = hash->find(ctx.cmd.args[1]);
    if (it == hash->end()) return RespCodec::Integer(0);
    return RespCodec::Integer(static_cast<int64_t>(it->second.size()));
}

} // anonymous namespace

void RegisterHashCommands(CommandRouter& r) {
    r.Register("HSET",    HSetCmd);
    r.Register("HGET",    HGetCmd);
    r.Register("HDEL",    HDelCmd);
    r.Register("HEXISTS", HExistsCmd);
    r.Register("HKEYS",   HKeysCmd);
    r.Register("HVALS",   HValsCmd);
    r.Register("HGETALL", HGetAllCmd);
    r.Register("HLEN",    HLenCmd);
    r.Register("HSTRLEN", HStrLenCmd);
}

}  // namespace redis
