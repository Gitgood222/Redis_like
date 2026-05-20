#pragma once

#include "../common.h"
#include "../dict.h"
#include <fstream>
#include <string>

namespace redis {

// ---------- RDB 快照持久化 ----------
// 全量序列化当前数据到 dump.rdb。
// 阶段 6 实现核心逻辑，阶段 1 仅定义接口骨架。
class RdbSaver {
public:
    explicit RdbSaver(const std::string& path) : path_(path) {}

    // 序列化整个 Dict 到文件，返回成功与否。
    bool Save(const Dict& db);

    // 从文件加载到 Dict，返回成功与否。
    bool Load(Dict& db);

private:
    std::string path_;

    // 序列化辅助
    void WriteType(std::ofstream& os, ObjType type);
    void WriteLength(std::ofstream& os, size_t len);
    void WriteString(std::ofstream& os, const std::string& s);
    void WriteDouble(std::ofstream& os, double val);
};

}  // namespace redis
