#pragma once

#include "common.h"
#include "event_loop.h"
#include "resp_codec.h"
#include "dict.h"
#include "expire.h"
#include "command/router.h"
#include "stats.h"
#include "storage/rdb.h"
#include "storage/aof.h"
#include <vector>
#include <string>

namespace redis {

struct Client {
    socket_t   fd = kInvalidSocket;
    RespCodec  codec;
    std::string write_buf;
    bool       close_after_write = false;
    bool       writable_mask = false;  // whether EPOLLOUT is set in the event loop
};

class RedisServer {
public:
    RedisServer();
    ~RedisServer();

    RedisServer(const RedisServer&) = delete;
    RedisServer& operator=(const RedisServer&) = delete;

    bool Init(int port = kDefaultPort);
    void Tick(int timeoutMs = 100);
    void Stop();

    Dict&           GetDb()      { return db_; }
    CommandRouter&  GetRouter()  { return router_; }
    bool            IsRunning() const { return running_; }

private:
    void OnAccept(int mask);
    void OnClientEvent(socket_t fd, int mask);
    void ProcessCommand(Client& client, RespCommand&& cmd);
    void SendResponse(Client& client, const std::string& resp);
    void SetupServerCron();
    void ServerCron();
    void CloseClient(socket_t fd);
    void FlushWriteBuf(Client& client);

    // persistence
    void LoadPersistedData();
    void AppendToAof(const RespCommand& cmd);

    // info
    std::string BuildInfoResponse(const std::string& section = "");

    EventLoop     loop_;
    socket_t      listen_fd_ = kInvalidSocket;
    Dict          db_;
    ExpireManager expire_;
    CommandRouter router_;
    Stats         stats_;

    RdbSaver rdb_{"dump.rdb"};
    AofLogger aof_{"appendonly.aof"};

    std::unordered_map<socket_t, Client> clients_;
    bool    running_  = false;
    int64_t cron_id_ = 0;
};

}  // namespace redis
