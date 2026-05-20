#pragma once

#include "../common.h"
#include "../dict.h"
#include "../object.h"
#include <fstream>
#include <string>

namespace redis {

// RDB file format constants
constexpr const char* kRdbMagic   = "REDIS0001";
constexpr uint8_t    kRdbVersion  = 1;
constexpr uint8_t    kRdbEof      = 0xFF;
constexpr uint8_t    kRdbExpireMs = 0xFC;  // followed by 8-byte ms timestamp
constexpr uint8_t    kRdbSelectDb = 0xFE;  // followed by db number

class RdbSaver {
public:
    explicit RdbSaver(const std::string& path) : path_(path) {}

    bool Save(const Dict& db);
    bool Load(Dict& db);

private:
    std::string path_;

    // ---- save helpers ----
    void WriteU8(std::ofstream& os, uint8_t v);
    void WriteU32(std::ofstream& os, uint32_t v);
    void WriteI64(std::ofstream& os, int64_t v);
    void WriteDouble(std::ofstream& os, double v);
    void WriteString(std::ofstream& os, const std::string& s);
    void WriteExpire(std::ofstream& os, const std::optional<TimePoint>& exp);

    void SaveObject(std::ofstream& os, const std::shared_ptr<RedisObject>& obj);
    void SaveString(std::ofstream& os, const RedisString& s);
    void SaveHash(std::ofstream& os, const RedisHash& h);
    void SaveList(std::ofstream& os, const RedisList& l);
    void SaveSet(std::ofstream& os, const RedisSet& s);
    void SaveZSet(std::ofstream& os, const std::shared_ptr<SkipList>& sl);

    // ---- load helpers ----
    uint8_t  ReadU8(std::ifstream& is);
    uint32_t ReadU32(std::ifstream& is);
    int64_t  ReadI64(std::ifstream& is);
    double   ReadDouble(std::ifstream& is);
    std::string ReadString(std::ifstream& is);

    std::optional<TimePoint> ReadExpire(std::ifstream& is);
    std::shared_ptr<RedisObject> LoadObject(std::ifstream& is, uint8_t type);
};

}  // namespace redis
