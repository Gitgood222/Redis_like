#include "server.h"
#include <iostream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

namespace redis {

// ---------- built-in commands ----------

void RegisterServerCommands(CommandRouter& r) {
    r.Register("PING", [](CmdContext& ctx) -> std::string {
        if (ctx.cmd.args.empty()) return RespCodec::Pong();
        return RespCodec::BulkString(ctx.cmd.args[0]);
    });

    r.Register("COMMAND", [](CmdContext&) -> std::string {
        return RespCodec::EmptyArray();
    });

    r.Register("QUIT", [](CmdContext&) -> std::string {
        return RespCodec::Ok();
    });
}

// ---------- RedisServer ----------

RedisServer::RedisServer() {
    RegisterServerCommands(router_);
    RegisterStringCommands(router_);
    RegisterKeyCommands(router_);
    RegisterHashCommands(router_);
    RegisterListCommands(router_);
    RegisterSetCommands(router_);
    RegisterZSetCommands(router_);
    aof_.Open();

    // INFO
    router_.Register("INFO", [this](CmdContext& ctx) -> std::string {
        std::string section;
        if (!ctx.cmd.args.empty()) section = ctx.cmd.args[0];
        return RespCodec::BulkString(BuildInfoResponse(section));
    });

    // ---------- Pub/Sub ----------

    router_.Register("SUBSCRIBE", [this](CmdContext& ctx) -> std::string {
        if (ctx.cmd.args.empty()) {
            return RespCodec::Error("ERR wrong number of arguments for 'subscribe' command");
        }
        std::string response;
        for (const auto& ch : ctx.cmd.args) {
            pubsub_.Subscribe(ctx.client_fd, ch);
            size_t count = pubsub_.GetClientChannels(ctx.client_fd).size();
            response += RespCodec::Array({
                RespCodec::BulkString("subscribe"),
                RespCodec::BulkString(ch),
                RespCodec::Integer(static_cast<int64_t>(count))
            });
        }
        return response;
    });

    router_.Register("UNSUBSCRIBE", [this](CmdContext& ctx) -> std::string {
        std::string response;
        if (ctx.cmd.args.empty()) {
            // Unsubscribe from all channels
            auto channels = pubsub_.GetClientChannels(ctx.client_fd);
            if (channels.empty()) {
                return RespCodec::Array({
                    RespCodec::BulkString("unsubscribe"),
                    RespCodec::NullBulkString(),
                    RespCodec::Integer(0)
                });
            }
            for (const auto& ch : channels) {
                pubsub_.Unsubscribe(ctx.client_fd, ch);
                size_t count = pubsub_.GetClientChannels(ctx.client_fd).size();
                response += RespCodec::Array({
                    RespCodec::BulkString("unsubscribe"),
                    RespCodec::BulkString(ch),
                    RespCodec::Integer(static_cast<int64_t>(count))
                });
            }
        } else {
            for (const auto& ch : ctx.cmd.args) {
                pubsub_.Unsubscribe(ctx.client_fd, ch);
                size_t count = pubsub_.GetClientChannels(ctx.client_fd).size();
                response += RespCodec::Array({
                    RespCodec::BulkString("unsubscribe"),
                    RespCodec::BulkString(ch),
                    RespCodec::Integer(static_cast<int64_t>(count))
                });
            }
        }
        return response;
    });

    router_.Register("PUBLISH", [this](CmdContext& ctx) -> std::string {
        if (ctx.cmd.args.size() != 2) {
            return RespCodec::Error("ERR wrong number of arguments for 'publish' command");
        }
        const auto& channel = ctx.cmd.args[0];
        const auto& message = ctx.cmd.args[1];

        auto push = [this](socket_t fd, const std::string& ch,
                           const std::string& msg, const std::string& pattern) {
            auto it = clients_.find(fd);
            if (it == clients_.end()) return;
            std::string resp;
            if (pattern.empty()) {
                resp = RespCodec::Array({
                    RespCodec::BulkString("message"),
                    RespCodec::BulkString(ch),
                    RespCodec::BulkString(msg)
                });
            } else {
                resp = RespCodec::Array({
                    RespCodec::BulkString("pmessage"),
                    RespCodec::BulkString(pattern),
                    RespCodec::BulkString(ch),
                    RespCodec::BulkString(msg)
                });
            }
            it->second.write_buf += resp;
            FlushWriteBuf(it->second);
        };

        int count = pubsub_.Publish(channel, message, push);
        return RespCodec::Integer(count);
    });

    router_.Register("PSUBSCRIBE", [this](CmdContext& ctx) -> std::string {
        if (ctx.cmd.args.empty()) {
            return RespCodec::Error("ERR wrong number of arguments for 'psubscribe' command");
        }
        std::string response;
        for (const auto& pat : ctx.cmd.args) {
            pubsub_.PSubscribe(ctx.client_fd, pat);
            size_t count = pubsub_.GetClientPatterns(ctx.client_fd).size();
            response += RespCodec::Array({
                RespCodec::BulkString("psubscribe"),
                RespCodec::BulkString(pat),
                RespCodec::Integer(static_cast<int64_t>(count))
            });
        }
        return response;
    });

    router_.Register("PUNSUBSCRIBE", [this](CmdContext& ctx) -> std::string {
        std::string response;
        if (ctx.cmd.args.empty()) {
            auto patterns = pubsub_.GetClientPatterns(ctx.client_fd);
            if (patterns.empty()) {
                return RespCodec::Array({
                    RespCodec::BulkString("punsubscribe"),
                    RespCodec::NullBulkString(),
                    RespCodec::Integer(0)
                });
            }
            for (const auto& pat : patterns) {
                pubsub_.PUnsubscribe(ctx.client_fd, pat);
                size_t count = pubsub_.GetClientPatterns(ctx.client_fd).size();
                response += RespCodec::Array({
                    RespCodec::BulkString("punsubscribe"),
                    RespCodec::BulkString(pat),
                    RespCodec::Integer(static_cast<int64_t>(count))
                });
            }
        } else {
            for (const auto& pat : ctx.cmd.args) {
                pubsub_.PUnsubscribe(ctx.client_fd, pat);
                size_t count = pubsub_.GetClientPatterns(ctx.client_fd).size();
                response += RespCodec::Array({
                    RespCodec::BulkString("punsubscribe"),
                    RespCodec::BulkString(pat),
                    RespCodec::Integer(static_cast<int64_t>(count))
                });
            }
        }
        return response;
    });
}

RedisServer::~RedisServer() {
    Stop();
}

bool RedisServer::Init(int port) {
    listen_fd_ = CreateListenSocket(port);
    if (listen_fd_ == kInvalidSocket) {
        std::cerr << "[ERROR] Failed to listen on port " << port << std::endl;
        return false;
    }
    std::cout << "[INFO] Listening on port " << port << std::endl;

    loop_.AddEvent(listen_fd_, kEventReadable,
        [this](int mask) { OnAccept(mask); });

    LoadPersistedData();

    SetupServerCron();

    running_ = true;
    std::cout << "[INFO] Server ready. Accepting connections..." << std::endl;
    return true;
}

void RedisServer::Stop() {
    running_ = false;

    // Save final snapshot
    if (db_.Size() > 0) {
        rdb_.Save(db_);
    }
    aof_.Close();

    if (listen_fd_ != kInvalidSocket) {
        loop_.RemoveEvent(listen_fd_);
        CLOSE_SOCKET(listen_fd_);
        listen_fd_ = kInvalidSocket;
    }
    for (auto& [fd, _] : clients_) {
        loop_.RemoveEvent(fd);
        CLOSE_SOCKET(fd);
    }
    clients_.clear();
}

void RedisServer::Tick(int timeoutMs) {
    if (!running_) return;
    loop_.RunOnce(timeoutMs);
}

// ---------- server cron ----------

void RedisServer::SetupServerCron() {
    using namespace std::chrono;
    cron_id_ = loop_.AddTimeEvent(
        milliseconds(100),   // first fire after 100ms
        milliseconds(100),   // then every 100ms (10 Hz, same as Redis)
        [this]() { ServerCron(); });
}

void RedisServer::ServerCron() {
    auto now = Clock::now();
    expire_.PeriodicCheck(db_, now);
}

// ---------- event callbacks ----------

void RedisServer::OnAccept(int /*mask*/) {
    while (true) {
        sockaddr_in addr{};
        socklen_t   addr_len = sizeof(addr);

        socket_t client_fd = accept(listen_fd_,
            reinterpret_cast<sockaddr*>(&addr), &addr_len);

        if (client_fd == kInvalidSocket) break;

        SetNonBlocking(client_fd);

        // Disable Nagle's algorithm for low latency
        int tcp_nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
#ifdef _WIN32
                   reinterpret_cast<const char*>(&tcp_nodelay), sizeof(tcp_nodelay));
#else
                   &tcp_nodelay, sizeof(tcp_nodelay));
#endif

        Client client;
        client.fd = client_fd;
        clients_[client_fd] = std::move(client);

        loop_.AddEvent(client_fd, kEventReadable,
            [this, client_fd](int mask) { OnClientEvent(client_fd, mask); });

        stats_.total_connections_received++;
        std::cout << "[INFO] New client (fd=" << client_fd << ")" << std::endl;
    }
}

void RedisServer::OnClientEvent(socket_t fd, int mask) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    auto& client = it->second;

    if (mask & kEventReadable) {
        char buf[kReadBufferSize];
        int n = recv(fd, buf, sizeof(buf), 0);

        if (n <= 0) {
            CloseClient(fd);
            return;
        }

        auto commands = client.codec.Feed(buf, static_cast<size_t>(n));
        for (auto& cmd : commands) {
            ProcessCommand(client, std::move(cmd));
        }
    }

    FlushWriteBuf(client);

    if (client.close_after_write && client.write_buf.empty()) {
        CloseClient(fd);
    }
}

// ---------- command processing ----------

void RedisServer::ProcessCommand(Client& client, RespCommand&& cmd) {
    std::string name = CommandRouter::Normalize(cmd.name);
    bool is_write = AofLogger::IsWriteCommand(name);

    CmdContext ctx{db_, expire_, std::move(cmd), Clock::now(), &stats_, &pubsub_, client.fd};
    stats_.total_commands_processed++;

    // Enforce pub/sub mode: subscribed clients can only run pub/sub commands
    if (pubsub_.IsSubscribed(client.fd)) {
        if (name != "SUBSCRIBE" && name != "UNSUBSCRIBE" &&
            name != "PSUBSCRIBE" && name != "PUNSUBSCRIBE" &&
            name != "PING" && name != "QUIT") {
            SendResponse(client, RespCodec::Error(
                "ERR only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT allowed in this context"));
            return;
        }
    }

    std::string response;
    if (name == "QUIT") {
        response = RespCodec::Ok();
        client.close_after_write = true;
    } else if (router_.Exists(name)) {
        response = router_.Execute(ctx);
    } else {
        response = RespCodec::Error("ERR unknown command '" + ctx.cmd.name + "'");
    }

    if (is_write && response.find("-ERR") != 0) {
        AppendToAof(ctx.cmd);
    }

    SendResponse(client, response);
}

void RedisServer::SendResponse(Client& client, const std::string& resp) {
    client.write_buf += resp;
}

void RedisServer::FlushWriteBuf(Client& client) {
    if (client.write_buf.empty()) return;

    int sent = send(client.fd, client.write_buf.data(),
                   static_cast<int>(client.write_buf.size()), 0);
    if (sent > 0) {
        client.write_buf.erase(0, sent);
    } else if (sent < 0) {
        // EAGAIN/EWOULDBLOCK — enable EPOLLOUT so we get notified when writable
        if (!client.writable_mask) {
            loop_.ModifyMask(client.fd, kEventReadable | kEventWritable);
            client.writable_mask = true;
        }
        return;
    }

    // Toggle EPOLLOUT based on whether pending data remains
    if (!client.write_buf.empty()) {
        if (!client.writable_mask) {
            loop_.ModifyMask(client.fd, kEventReadable | kEventWritable);
            client.writable_mask = true;
        }
    } else {
        if (client.writable_mask) {
            loop_.ModifyMask(client.fd, kEventReadable);
            client.writable_mask = false;
        }
    }
}

void RedisServer::CloseClient(socket_t fd) {
    pubsub_.UnsubscribeAll(fd);
    loop_.RemoveEvent(fd);
    CLOSE_SOCKET(fd);
    clients_.erase(fd);
    std::cout << "[INFO] Client disconnected (fd=" << fd << ")" << std::endl;
}

// ---------- persistence ----------

void RedisServer::LoadPersistedData() {
    // Load RDB first, then replay AOF on top
    rdb_.Load(db_);

    aof_.Replay([this](RespCommand& cmd) {
        std::string name = CommandRouter::Normalize(cmd.name);
        if (name == "QUIT" || name == "PING" || name == "COMMAND") return;
        CmdContext ctx{db_, expire_, std::move(cmd), Clock::now()};
        if (router_.Exists(name)) {
            router_.Execute(ctx);
        }
    });

    std::cout << "[INFO] Data loaded: " << db_.Size() << " keys" << std::endl;
}

void RedisServer::AppendToAof(const RespCommand& cmd) {
    // Re-serialize command to RESP and append
    std::vector<std::string> parts;
    parts.push_back(RespCodec::BulkString(cmd.name));
    for (const auto& arg : cmd.args) {
        parts.push_back(RespCodec::BulkString(arg));
    }
    aof_.Append(RespCodec::Array(parts));
}

// ---------- info ----------

std::string RedisServer::BuildInfoResponse(const std::string& section) {
    auto now = Clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - stats_.start_time).count();

    // Count keys with expiry
    int64_t keys_with_expire = 0;
    for (const auto& key : db_.Keys()) {
        auto obj = db_.Get(key);
        if (obj && obj->expire_at) keys_with_expire++;
    }

    std::ostringstream ss;

    auto emit = [&](const std::string& sec, const std::string& line) {
        if (section.empty() || section == sec) {
            ss << line << "\r\n";
        }
    };

    // Server
    emit("server", "# Server");
    emit("server", "redis_version:1.0.0");
    emit("server", "uptime_in_seconds:" + std::to_string(uptime));
    emit("server", "");

    // Clients
    emit("clients", "# Clients");
    emit("clients", "connected_clients:" + std::to_string(clients_.size()));
    emit("clients", "");

    // Stats
    emit("stats", "# Stats");
    emit("stats", "total_commands_processed:" +
         std::to_string(stats_.total_commands_processed));
    emit("stats", "total_connections_received:" +
         std::to_string(stats_.total_connections_received));
    emit("stats", "keyspace_hits:" + std::to_string(stats_.keyspace_hits));
    emit("stats", "keyspace_misses:" + std::to_string(stats_.keyspace_misses));
    emit("stats", "expired_keys:" + std::to_string(stats_.expired_keys));
    emit("stats", "");

    // Persistence
    emit("persistence", "# Persistence");
    emit("persistence", "rdb_enabled:1");
    emit("persistence", "aof_enabled:1");
    emit("persistence", "");

    // Keyspace
    emit("keyspace", "# Keyspace");
    emit("keyspace", "db0:keys=" + std::to_string(db_.Size()) +
         ",expires=" + std::to_string(keys_with_expire));
    emit("keyspace", "");

    return ss.str();
}

}  // namespace redis
