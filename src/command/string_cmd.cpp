#include "router.h"
#include <cstdlib>
#include <vector>

namespace redis {

// ---------- helpers ----------

// Check and perform lazy expiry. Returns true if key was expired/deleted.
static bool CheckExpire(CmdContext& ctx, const std::string& key) {
    bool expired = ctx.expire.LazyCheck(ctx.db, key, ctx.now);
    if (expired) ctx.RecordExpired();
    return expired;
}

// Parse a string argument as int64. Returns nullopt on failure.
static std::optional<int64_t> ParseInt(const std::string& s) {
    char* end = nullptr;
    int64_t v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

// Glob pattern matcher supporting *, ?, [abc], [^abc], and [a-z] ranges.
static bool MatchGlob(const std::string& str, const std::string& pattern) {
    size_t si = 0, pi = 0;
    size_t starIdx = std::string::npos;
    size_t matchIdx = 0;

    while (si < str.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == str[si])) {
            ++si;
            ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starIdx = pi;
            matchIdx = si;
            ++pi;
        } else if (starIdx != std::string::npos) {
            pi = starIdx + 1;
            ++matchIdx;
            si = matchIdx;
        } else if (pi < pattern.size() && pattern[pi] == '[') {
            size_t close = pattern.find(']', pi);
            if (close == std::string::npos) {
                if (pattern[pi] != str[si]) return false;
                ++si;
                ++pi;
            } else {
                bool negate = (pi + 1 < pattern.size() && pattern[pi + 1] == '^');
                size_t cs = negate ? pi + 2 : pi + 1;
                bool matched = false;
                for (size_t ci = cs; ci < close; ++ci) {
                    if (ci + 2 < close && pattern[ci + 1] == '-') {
                        if (str[si] >= pattern[ci] && str[si] <= pattern[ci + 2]) {
                            matched = true;
                            break;
                        }
                        ci += 2;
                    } else if (pattern[ci] == str[si]) {
                        matched = true;
                        break;
                    }
                }
                if (negate) matched = !matched;
                if (!matched) {
                    if (starIdx != std::string::npos) {
                        pi = starIdx + 1;
                        ++matchIdx;
                        si = matchIdx;
                        continue;
                    }
                    return false;
                }
                ++si;
                pi = close + 1;
            }
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
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
        ctx.RecordMiss();
        return RespCodec::NullBulkString();
    }

    ctx.RecordHit();
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
        if (ctx.db.Exists(key)) { ++count; ctx.RecordHit(); }
        else ctx.RecordMiss();
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
    if (!obj) { ctx.RecordMiss(); return RespCodec::Integer(0); }
    ctx.RecordHit();

    if (*secs > 0) {
        ctx.expire.SetExpire(obj, ctx.now + std::chrono::seconds(*secs));
    } else {
        ctx.expire.RemoveExpire(obj);
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
    if (!obj) ctx.RecordMiss(); else ctx.RecordHit();
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
    if (!obj) { ctx.RecordMiss(); return RespCodec::Integer(0); }
    ctx.RecordHit();

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

    if (!obj) { ctx.RecordMiss(); return RespCodec::Integer(-2); }
    ctx.RecordHit();
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
    if (!obj) { ctx.RecordMiss(); return RespCodec::SimpleString("none"); }
    ctx.RecordHit();
    return RespCodec::SimpleString(ObjTypeName(obj->type));
}

// KEYS pattern
static std::string KeysCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty()) {
        return RespCodec::Error("ERR wrong number of arguments for 'KEYS' command");
    }

    const std::string& pattern = ctx.cmd.args[0];

    // Collect keys first to avoid iterator invalidation during lazy deletion
    auto keys = ctx.db.Keys();
    std::vector<std::string> matches;
    for (const auto& key : keys) {
        auto obj = ctx.db.Get(key);
        if (obj && obj->IsExpired(ctx.now)) {
            ctx.db.Del(key);
            continue;
        }
        if (!obj) continue;
        if (MatchGlob(key, pattern)) {
            matches.push_back(key);
        }
    }

    std::vector<std::string> resp_items;
    resp_items.reserve(matches.size());
    for (const auto& k : matches) {
        resp_items.push_back(RespCodec::BulkString(k));
    }
    return RespCodec::Array(resp_items);
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
    r.Register("KEYS",   KeysCmd);
}

}  // namespace redis
