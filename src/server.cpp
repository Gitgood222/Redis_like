#include "server.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
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
    PeriodicExpireCheck();
}

// ---------- periodic expiry ----------

void RedisServer::PeriodicExpireCheck() {
    auto now = Clock::now();
    if (now - last_expire_check_ < std::chrono::milliseconds(100)) return;
    last_expire_check_ = now;
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

        Client client;
        client.fd = client_fd;
        clients_[client_fd] = std::move(client);

        loop_.AddEvent(client_fd, kEventReadable,
            [this, client_fd](int mask) { OnClientEvent(client_fd, mask); });

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

    CmdContext ctx{db_, expire_, std::move(cmd), Clock::now()};

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
        // errno would tell us more; for now, ignore
    }
}

void RedisServer::CloseClient(socket_t fd) {
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

}  // namespace redis
