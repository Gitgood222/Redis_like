#pragma once

#include "../common.h"
#include "../resp_codec.h"
#include <fstream>
#include <functional>
#include <string>
#include <unordered_set>

namespace redis {

class AofLogger {
public:
    explicit AofLogger(const std::string& path) : path_(path) {}

    bool Open();
    void Append(const std::string& respCmd);
    void Flush();
    void Close();

    // Replay AOF file: for each command, call the callback.
    // Returns number of commands replayed.
    int  Replay(std::function<void(RespCommand&)> callback);

    // Check if a command name is a write operation that should be logged.
    static bool IsWriteCommand(const std::string& name);

private:
    std::string   path_;
    std::ofstream file_;
};

}  // namespace redis
