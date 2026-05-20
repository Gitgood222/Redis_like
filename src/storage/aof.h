#pragma once

#include "../common.h"
#include "../resp_codec.h"
#include <fstream>
#include <string>

namespace redis {

// ---------- AOF 日志持久化 ----------
// 将每条写命令追加到 appendonly.aof。
// 阶段 6 实现核心逻辑，阶段 1 仅定义接口骨架。
class AofLogger {
public:
    explicit AofLogger(const std::string& path) : path_(path) {}

    // 打开 AOF 文件。
    bool Open();

    // 追加一条 RESP 格式的命令。
    void Append(const RespCommand& cmd);

    // 回放 AOF 文件中的命令（通过回调执行）。
    // callback 接收每个 RespCommand，返回执行后的 RESP 响应。
    void Rewrite(std::function<void(const RespCommand&)> callback);

    // fsync 到磁盘。
    void Flush();

    void Close();

private:
    std::string   path_;
    std::ofstream file_;
};

}  // namespace redis
