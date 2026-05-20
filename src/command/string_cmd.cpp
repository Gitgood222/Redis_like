#include "router.h"
#include <cstdlib>
#include <vector>

namespace redis {

// ---------- helpers ----------

// Check and perform lazy expiry. Returns true if key was expired/deleted.
static bool CheckExpire(CmdContext& ctx, const std::string& key) {
    return ctx.expire.LazyCheck(ctx.db, key, ctx.now);
}

// Parse a string argument as int64. Returns nullopt on failure.
static std::optional<int64_t> ParseInt(const std::string& s) {
    char* end = nullptr;
    int64_t v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

// ---------- String Commands ----------

// SET key value [EX seconds|PX milliseconds] [NX|XX]
static std::string SetCmd(CmdContext& ctx) {
    auto& args = ctx.cmd.args;
    if (args.size() < 2) {
        return RespCodec::Error("ERR wrong number of arguments for 'SET' command");
    }

    const std::string& key = args[0];
    const std::string& val = args[1];

    // parse options
    enum { kNone, kEx, kPx } expiry_mode = kNone;
    int64_t expiry_ms = 0;
    bool nx = false, xx = false;

    for (size_t i = 2; i < args.size(); ++i) {
        std::string opt = args[i];
        for (auto& c : opt) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (opt == "NX") {
            nx = true;
        } else if (opt == "XX") {
            xx = true;
        } else if (opt == "EX") {
            if (i + 1 >= args.size()) {
                return RespCodec::Error("ERR syntax error");
            }
            auto v = ParseInt(args[++i]);
            if (!v || *v <= 0) {
                return RespCodec::Error("ERR invalid expire time in 'SET'");
            }
            expiry_mode = kEx;
            expiry_ms = *v * 1000;
        } else if (opt == "PX") {
            if (i + 1 >= args.size()) {
                return RespCodec::Error("ERR syntax error");
            }
            auto v = ParseInt(args[++i]);
            if (!v || *v <= 0) {
                return RespCodec::Error("ERR invalid expire time in 'SET'");
            }
            expiry_mode = kPx;
            expiry_ms = *v;
        } else {
            return RespCodec::Error("ERR syntax error");
        }
    }

    if (nx && xx) {
        return RespCodec::Error("ERR NX and XX options are mutually exclusive");
    }

    bool exists = ctx.db.Exists(key);
    CheckExpire(ctx, key);
    // Re-check after expire
    exists = ctx.db.Exists(key);

    if (nx && exists)  return RespCodec::NullBulkString();
    if (xx && !exists) return RespCodec::NullBulkString();

    auto obj = RedisObject::CreateString(val);

    if (expiry_mode != kNone) {
        ctx.expire.SetExpire(obj, ctx.now + std::chrono::milliseconds(expiry_ms));
    }

    ctx.db.Set(key, std::move(obj));
    return RespCodec::Ok();
}

// GET key
static std::string GetCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'GET' command");
    }

    const std::string& key = ctx.cmd.args[0];
    CheckExpire(ctx, key);

    auto obj = ctx.db.Get(key);
    if (!obj || obj->type != ObjType::kString) {
        return RespCodec::NullBulkString();
    }

    auto* s = obj->As<RedisString>();
    if (!s) return RespCodec::NullBulkString();
    return RespCodec::BulkString(*s);
}

// ---------- Key Commands ----------

// DEL key [key ...]
static std::string DelCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'DEL' command");
    }

    int64_t count = 0;
    for (const auto& key : ctx.cmd.args) {
        if (ctx.db.Del(key)) ++count;
    }
    return RespCodec::Integer(count);
}

// EXISTS key [key ...]
static std::string ExistsCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'EXISTS' command");
    }

    int64_t count = 0;
    for (const auto& key : ctx.cmd.args) {
        CheckExpire(ctx, key);
        if (ctx.db.Exists(key)) ++count;
    }
    return RespCodec::Integer(count);
}

// EXPIRE key seconds
static std::string ExpireCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2) {
        return RespCodec::Error("ERR wrong number of arguments for 'EXPIRE' command");
    }

    const std::string& key = ctx.cmd.args[0];
    auto secs = ParseInt(ctx.cmd.args[1]);
    if (!secs) {
        return RespCodec::Error("ERR value is not an integer");
    }

    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) return RespCodec::Integer(0);

    if (*secs > 0) {
        ctx.expire.SetExpire(obj, ctx.now + std::chrono::seconds(*secs));
    } else {
        ctx.expire.RemoveExpire(obj);
        // Negative expire = delete the key (Redis behavior)
        ctx.db.Del(key);
    }
    return RespCodec::Integer(1);
}

// TTL key
static std::string TtlCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'TTL' command");
    }

    const std::string& key = ctx.cmd.args[0];
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    int64_t ttl = ctx.expire.GetTTL(obj, ctx.now);
    return RespCodec::Integer(ttl);
}

// PEXPIRE key milliseconds
static std::string PExpireCmd(CmdContext& ctx) {
    if (ctx.cmd.args.size() < 2) {
        return RespCodec::Error("ERR wrong number of arguments for 'PEXPIRE' command");
    }

    const std::string& key = ctx.cmd.args[0];
    auto ms = ParseInt(ctx.cmd.args[1]);
    if (!ms) {
        return RespCodec::Error("ERR value is not an integer");
    }

    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) return RespCodec::Integer(0);

    if (*ms > 0) {
        ctx.expire.SetExpire(obj, ctx.now + std::chrono::milliseconds(*ms));
    } else {
        ctx.expire.RemoveExpire(obj);
        ctx.db.Del(key);
    }
    return RespCodec::Integer(1);
}

// PTTL key
static std::string PTtlCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'PTTL' command");
    }

    const std::string& key = ctx.cmd.args[0];
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);

    if (!obj) return RespCodec::Integer(-2);
    if (!obj->expire_at) return RespCodec::Integer(-1);

    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        *obj->expire_at - ctx.now).count();
    return RespCodec::Integer(remaining >= 0 ? remaining : -2);
}

// TYPE key
static std::string TypeCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'TYPE' command");
    }

    const std::string& key = ctx.cmd.args[0];
    CheckExpire(ctx, key);
    auto obj = ctx.db.Get(key);
    if (!obj) return RespCodec::SimpleString("none");

    return RespCodec::SimpleString(ObjTypeName(obj->type));
}

// ---------- Registration ----------

void RegisterStringCommands(CommandRouter& r) {
    r.Register("SET", SetCmd);
    r.Register("GET", GetCmd);
}

void RegisterKeyCommands(CommandRouter& r) {
    r.Register("DEL",    DelCmd);
    r.Register("EXISTS", ExistsCmd);
    r.Register("EXPIRE", ExpireCmd);
    r.Register("TTL",    TtlCmd);
    r.Register("PEXPIRE", PExpireCmd);
    r.Register("PTTL",   PTtlCmd);
    r.Register("TYPE",   TypeCmd);
}

}  // namespace redis
