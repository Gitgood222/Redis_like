#include "resp_codec.h"
#include <charconv>
#include <cstdlib>

namespace redis {

RespCodec::RespCodec() {}

std::optional<RespType> RespCodec::CharToType(char c) const {
    switch (c) {
        case '+': return RespType::kSimpleString;
        case '-': return RespType::kError;
        case ':': return RespType::kInteger;
        case '$': return RespType::kBulkString;
        case '*': return RespType::kArray;
        default:  return std::nullopt;
    }
}

static bool ReadLine(const std::string& buf, size_t& pos, std::string& out) {
    size_t rn = buf.find("\r\n", pos);
    if (rn == std::string::npos) return false;
    out = buf.substr(pos, rn - pos);
    pos = rn + 2;
    return true;
}

std::vector<RespCommand> RespCodec::Feed(const char* data, size_t len) {
    read_buf_.append(data, len);
    std::vector<RespCommand> results;
    size_t pos = 0;

    while (pos < read_buf_.size()) {
        // checkpoint allows rollback when data is incomplete
        size_t checkpoint = pos;

        if (read_buf_[pos] != '*') {
            ++pos;
            continue;
        }
        pos++; // consume '*'

        // read array length
        std::string line;
        if (!ReadLine(read_buf_, pos, line)) {
            pos = checkpoint;
            break;
        }

        int64_t array_len = std::strtoll(line.c_str(), nullptr, 10);
        if (array_len <= 0 || array_len > 256) {
            // invalid — skip
            continue;
        }

        RespCommand cmd;
        bool ok = true;

        for (int64_t i = 0; i < array_len; ++i) {
            if (pos >= read_buf_.size() || read_buf_[pos] != '$') {
                ok = false;
                break;
            }
            pos++; // consume '$'

            if (!ReadLine(read_buf_, pos, line)) {
                ok = false;
                break;
            }

            int64_t bulk_len = std::strtoll(line.c_str(), nullptr, 10);
            if (bulk_len < 0) bulk_len = 0;

            if (pos + bulk_len + 2 > read_buf_.size()) {
                ok = false;
                break;
            }

            std::string token(read_buf_.data() + pos, bulk_len);
            pos += bulk_len;

            if (pos + 1 < read_buf_.size()
                && read_buf_[pos] == '\r' && read_buf_[pos + 1] == '\n') {
                pos += 2;
            }

            if (i == 0) {
                cmd.name = std::move(token);
            } else {
                cmd.args.push_back(std::move(token));
            }
        }

        if (ok) {
            results.push_back(std::move(cmd));
        } else {
            // Incomplete — roll back to before the '*'
            pos = checkpoint;
            break;
        }
    }

    read_buf_.erase(0, pos);
    return results;
}

// ---------- serializers ----------

std::string RespCodec::SimpleString(std::string_view s) {
    std::string r;
    r.reserve(s.size() + 3);
    r += '+';
    r += s;
    r += "\r\n";
    return r;
}

std::string RespCodec::Error(std::string_view msg) {
    std::string r;
    r.reserve(msg.size() + 3);
    r += '-';
    r += msg;
    r += "\r\n";
    return r;
}

std::string RespCodec::Integer(int64_t n) {
    std::string s = std::to_string(n);
    std::string r;
    r.reserve(s.size() + 3);
    r += ':';
    r += s;
    r += "\r\n";
    return r;
}

std::string RespCodec::BulkString(std::string_view s) {
    std::string r;
    r += '$';
    r += std::to_string(s.size());
    r += "\r\n";
    r += s;
    r += "\r\n";
    return r;
}

std::string RespCodec::NullBulkString() {
    return "$-1\r\n";
}

std::string RespCodec::Array(const std::vector<std::string>& items) {
    std::string r;
    r += '*';
    r += std::to_string(items.size());
    r += "\r\n";
    for (const auto& item : items) {
        r += item;
    }
    return r;
}

std::string RespCodec::NullArray() {
    return "*-1\r\n";
}

}  // namespace redis
