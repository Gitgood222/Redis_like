#pragma once

#include "common.h"
#include "event_loop.h"
#include "resp_codec.h"
#include "dict.h"
#include "expire.h"
#include "command/router.h"
#include <vector>
#include <string>

namespace redis {

// ---------- 客户端连接状态 ----------
struct Client {
    socket_t   fd = kInvalidSocket;
    RespCodec  codec;
    std::string write_buf;        // pending outgoing data
    bool       close_after_write = false;
};

// ---------- RedisServer ----------
class RedisServer {
public:
    RedisServer();
    ~RedisServer();

    RedisServer(const RedisServer&) = delete;
    RedisServer& operator=(const RedisServer&) = delete;

    // 初始化监听，绑定端口。返回 false 表示失败。
    bool Init(int port = kDefaultPort);

    // 处理一次事件循环迭代（非阻塞，timeoutMs 内返回）。
    void Tick(int timeoutMs = 100);

    void Stop();

    Dict&           GetDb()    { return db_; }
    CommandRouter&  GetRouter() { return router_; }
    bool            IsRunning() const { return running_; }

private:
    // event callbacks
    void OnAccept(int mask);
    void OnClientEvent(socket_t fd, int mask);

    // process a complete command
    void ProcessCommand(Client& client, RespCommand&& cmd);

    // send a RESP string to a client
    void SendResponse(Client& client, const std::string& resp);

    // close a client connection
    void CloseClient(socket_t fd);

    // flush pending writes for a client
    void FlushWriteBuf(Client& client);

    void PeriodicExpireCheck();

    EventLoop     loop_;
    socket_t      listen_fd_ = kInvalidSocket;
    Dict          db_;
    ExpireManager expire_;
    CommandRouter router_;
    TimePoint     last_expire_check_ = Clock::now();

    std::unordered_map<socket_t, Client> clients_;
    bool running_ = false;
};

}  // namespace redis
