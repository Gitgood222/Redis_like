#pragma once

#include "../common.h"
#include "../event_loop.h"
#include "../object.h"
#include "../dict.h"
#include "../expire.h"
#include "../resp_codec.h"
#include "../stats.h"
#include <unordered_map>
#include <functional>

namespace redis {

class PubSubManager;

// ---------- 命令上下文 ----------
struct CmdContext {
    Dict&          db;
    ExpireManager& expire;
    RespCommand    cmd;
    TimePoint      now;         // current time for expiry checks
    Stats*         stats = nullptr;
    PubSubManager* pubsub = nullptr;
    socket_t       client_fd = kInvalidSocket;

    void RecordHit()     { if (stats) stats->RecordHit(); }
    void RecordMiss()    { if (stats) stats->RecordMiss(); }
    void RecordExpired() { if (stats) stats->RecordExpired(); }
};

// 命令处理器：接收上下文，返回 RESP 格式的响应字符串
using CmdHandler = std::function<std::string(CmdContext&)>;

// ---------- 命令路由表 ----------
class CommandRouter {
public:
    CommandRouter();

    // Register a command.
    void Register(const std::string& name, CmdHandler handler);

    // Execute a command, returning the RESP response.
    // Returns nullptr if command not found.
    std::string Execute(CmdContext& ctx) const;

    // Check if a command exists.
    bool Exists(const std::string& name) const { return table_.find(name) != table_.end(); }

    // Make a name uppercase for case-insensitive matching.
    static std::string Normalize(std::string_view name);

private:
    std::unordered_map<std::string, CmdHandler> table_;
};

// ---------- 预置命令注册 ----------
void RegisterStringCommands(CommandRouter& router);
void RegisterHashCommands(CommandRouter& router);
void RegisterListCommands(CommandRouter& router);
void RegisterSetCommands(CommandRouter& router);
void RegisterZSetCommands(CommandRouter& router);
void RegisterKeyCommands(CommandRouter& router);
void RegisterServerCommands(CommandRouter& router);

}  // namespace redis
