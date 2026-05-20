#include "aof.h"
#include <iostream>
#include <fstream>
#include <cctype>

namespace redis {

bool AofLogger::Open() {
    // Binary mode prevents Windows \n → \r\n conversion (RESP already uses \r\n)
    file_.open(path_, std::ios::app | std::ios::out | std::ios::binary);
    if (!file_) {
        std::cerr << "[AOF] Cannot open " << path_ << std::endl;
        return false;
    }
    return true;
}

void AofLogger::Append(const std::string& respCmd) {
    if (file_.is_open()) {
        file_ << respCmd;
        file_.flush();  // ensure it's written (can optimize to periodic fsync)
    }
}

void AofLogger::Flush() {
    if (file_.is_open()) file_.flush();
}

void AofLogger::Close() {
    if (file_.is_open()) file_.close();
}

int AofLogger::Replay(std::function<void(RespCommand&)> callback) {
    std::ifstream is(path_, std::ios::binary);
    if (!is) return 0;

    // Read entire file as binary (preserve \r\n)
    std::string content;
    is.seekg(0, std::ios::end);
    content.resize(is.tellg());
    is.seekg(0, std::ios::beg);
    is.read(content.data(), content.size());

    RespCodec codec;
    auto cmds = codec.Feed(content.data(), content.size());

    for (auto& cmd : cmds) {
        if (!cmd.name.empty()) {
            callback(cmd);
        }
    }

    return static_cast<int>(cmds.size());
}

bool AofLogger::IsWriteCommand(const std::string& name) {
    // Normalize to uppercase
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    static const std::unordered_set<std::string> writes = {
        "SET", "DEL",
        "HSET", "HDEL",
        "LPUSH", "RPUSH", "LPOP", "RPOP",
        "SADD", "SREM", "SPOP",
        "ZADD", "ZREM",
        "EXPIRE", "PEXPIRE",
    };
    return writes.count(upper) > 0;
}

}  // namespace redis
