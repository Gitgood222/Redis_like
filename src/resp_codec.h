#pragma once

#include "common.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace redis {

enum class RespType : uint8_t {
    kSimpleString = '+',
    kError        = '-',
    kInteger      = ':',
    kBulkString   = '$',
    kArray        = '*',
};

struct RespCommand {
    std::string name;
    std::vector<std::string> args;
};

class RespCodec {
public:
    RespCodec();

    // Feed raw bytes; returns zero or more parsed commands (pipelining).
    std::vector<RespCommand> Feed(const char* data, size_t len);

    // ---------- RESP serializers ----------
    static std::string SimpleString(std::string_view s);
    static std::string Error(std::string_view msg);
    static std::string Integer(int64_t n);
    static std::string BulkString(std::string_view s);
    static std::string NullBulkString();
    static std::string Array(const std::vector<std::string>& items);
    static std::string NullArray();

    static std::string Ok()   { return SimpleString("OK"); }
    static std::string Pong() { return BulkString("PONG"); }
    static std::string EmptyArray() { return Array({}); }

private:
    std::optional<RespType> CharToType(char c) const;
    std::string read_buf_;
};

}  // namespace redis
