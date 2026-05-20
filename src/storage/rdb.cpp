#include "rdb.h"
#include "../ds/skiplist.h"
#include <cstring>
#include <iostream>

namespace redis {

// ---------- primitive writers ----------

void RdbSaver::WriteU8(std::ofstream& os, uint8_t v) {
    os.write(reinterpret_cast<const char*>(&v), 1);
}

void RdbSaver::WriteU32(std::ofstream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

void RdbSaver::WriteI64(std::ofstream& os, int64_t v) {
    os.write(reinterpret_cast<const char*>(&v), 8);
}

void RdbSaver::WriteDouble(std::ofstream& os, double v) {
    os.write(reinterpret_cast<const char*>(&v), 8);
}

void RdbSaver::WriteString(std::ofstream& os, const std::string& s) {
    WriteU32(os, static_cast<uint32_t>(s.size()));
    os.write(s.data(), s.size());
}

void RdbSaver::WriteExpire(std::ofstream& os,
                            const std::optional<TimePoint>& exp) {
    if (exp) {
        WriteU8(os, kRdbExpireMs);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            exp->time_since_epoch()).count();
        WriteI64(os, ms);
    }
}

// ---------- save by type ----------

void RdbSaver::SaveString(std::ofstream& os, const RedisString& s) {
    WriteU8(os, static_cast<uint8_t>(ObjType::kString));
    WriteString(os, s);
}

void RdbSaver::SaveHash(std::ofstream& os, const RedisHash& h) {
    WriteU8(os, static_cast<uint8_t>(ObjType::kHash));
    WriteU32(os, static_cast<uint32_t>(h.size()));
    for (const auto& kv : h) {
        WriteString(os, kv.first);
        WriteString(os, kv.second);
    }
}

void RdbSaver::SaveList(std::ofstream& os, const RedisList& l) {
    WriteU8(os, static_cast<uint8_t>(ObjType::kList));
    WriteU32(os, static_cast<uint32_t>(l.size()));
    for (const auto& val : l) {
        WriteString(os, val);
    }
}

void RdbSaver::SaveSet(std::ofstream& os, const RedisSet& s) {
    WriteU8(os, static_cast<uint8_t>(ObjType::kSet));
    WriteU32(os, static_cast<uint32_t>(s.size()));
    for (const auto& member : s) {
        WriteString(os, member);
    }
}

void RdbSaver::SaveZSet(std::ofstream& os, const std::shared_ptr<SkipList>& sl) {
    WriteU8(os, static_cast<uint8_t>(ObjType::kZSet));
    auto members = sl->RangeByRank(0, static_cast<int64_t>(sl->Size()) - 1);
    WriteU32(os, static_cast<uint32_t>(members.size()));
    for (const auto& m : members) {
        auto score = sl->GetScore(m);
        WriteString(os, m);
        WriteDouble(os, *score);
    }
}

void RdbSaver::SaveObject(std::ofstream& os,
                           const std::shared_ptr<RedisObject>& obj) {
    WriteExpire(os, obj->expire_at);

    switch (obj->type) {
    case ObjType::kString: SaveString(os, *obj->As<RedisString>()); break;
    case ObjType::kHash:   SaveHash(os, *obj->As<RedisHash>());     break;
    case ObjType::kList:   SaveList(os, *obj->As<RedisList>());     break;
    case ObjType::kSet:    SaveSet(os, *obj->As<RedisSet>());       break;
    case ObjType::kZSet:   SaveZSet(os, *obj->As<std::shared_ptr<SkipList>>()); break;
    }
}

// ---------- public save / load ----------

bool RdbSaver::Save(const Dict& db) {
    std::ofstream os(path_, std::ios::binary | std::ios::trunc);
    if (!os) {
        std::cerr << "[RDB] Cannot open " << path_ << " for writing" << std::endl;
        return false;
    }

    // Header
    os.write(kRdbMagic, 9);
    WriteU8(os, kRdbVersion);

    // Database selector
    WriteU8(os, kRdbSelectDb);
    WriteU32(os, 0);

    // Key-value pairs
    for (const auto& kv : db) {
        WriteString(os, kv.first);      // key
        SaveObject(os, kv.second);      // expire + type + value
    }

    // EOF
    WriteU8(os, kRdbEof);
    // 8-byte checksum placeholder
    WriteI64(os, 0);

    std::cout << "[RDB] Saved " << db.Size() << " keys to " << path_ << std::endl;
    return true;
}

// ---------- primitive readers ----------

uint8_t RdbSaver::ReadU8(std::ifstream& is) {
    uint8_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 1);
    return v;
}

uint32_t RdbSaver::ReadU32(std::ifstream& is) {
    uint32_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

int64_t RdbSaver::ReadI64(std::ifstream& is) {
    int64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

double RdbSaver::ReadDouble(std::ifstream& is) {
    double v = 0;
    is.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

std::string RdbSaver::ReadString(std::ifstream& is) {
    uint32_t len = ReadU32(is);
    std::string s(len, '\0');
    is.read(s.data(), len);
    return s;
}

std::optional<TimePoint> RdbSaver::ReadExpire(std::ifstream& is) {
    uint8_t marker = ReadU8(is);
    if (marker == kRdbExpireMs) {
        int64_t ms = ReadI64(is);
        return TimePoint(std::chrono::milliseconds(ms));
    }
    // Put back the marker (it's the type byte)
    is.seekg(-1, std::ios::cur);
    return std::nullopt;
}

std::shared_ptr<RedisObject> RdbSaver::LoadObject(std::ifstream& is,
                                                    uint8_t type) {
    switch (static_cast<ObjType>(type)) {
    case ObjType::kString: {
        std::string val = ReadString(is);
        return RedisObject::CreateString(std::move(val));
    }
    case ObjType::kHash: {
        auto obj = RedisObject::CreateHash();
        auto* h = obj->As<RedisHash>();
        uint32_t count = ReadU32(is);
        for (uint32_t i = 0; i < count; ++i) {
            std::string field = ReadString(is);
            std::string val   = ReadString(is);
            (*h)[std::move(field)] = std::move(val);
        }
        return obj;
    }
    case ObjType::kList: {
        auto obj = RedisObject::CreateList();
        auto* l = obj->As<RedisList>();
        uint32_t count = ReadU32(is);
        for (uint32_t i = 0; i < count; ++i) {
            l->push_back(ReadString(is));
        }
        return obj;
    }
    case ObjType::kSet: {
        auto obj = RedisObject::CreateSet();
        auto* s = obj->As<RedisSet>();
        uint32_t count = ReadU32(is);
        for (uint32_t i = 0; i < count; ++i) {
            s->insert(ReadString(is));
        }
        return obj;
    }
    case ObjType::kZSet: {
        auto obj = RedisObject::CreateZSet();
        auto* sl = obj->As<std::shared_ptr<SkipList>>()->get();
        uint32_t count = ReadU32(is);
        for (uint32_t i = 0; i < count; ++i) {
            std::string member = ReadString(is);
            double score = ReadDouble(is);
            sl->Insert(score, member);
        }
        return obj;
    }
    }
    return nullptr;
}

bool RdbSaver::Load(Dict& db) {
    std::ifstream is(path_, std::ios::binary);
    if (!is) return false;  // file doesn't exist, not an error

    // Verify magic
    char magic[10]{};
    is.read(magic, 9);
    if (std::memcmp(magic, kRdbMagic, 9) != 0) {
        std::cerr << "[RDB] Bad magic in " << path_ << std::endl;
        return false;
    }

    uint8_t version = ReadU8(is);
    (void)version;

    // Skip selectdb marker
    uint8_t marker = ReadU8(is);
    if (marker == kRdbSelectDb) {
        ReadU32(is);  // db number
    } else {
        is.seekg(-1, std::ios::cur);
    }

    while (is) {
        // Try to read key; check if it's EOF
        auto peek = is.peek();
        if (peek == EOF || static_cast<uint8_t>(peek) == kRdbEof) break;

        std::string key = ReadString(is);

        // Read expire (might be a marker before type)
        auto expire_at = ReadExpire(is);

        uint8_t type = ReadU8(is);
        auto obj = LoadObject(is, type);
        if (obj) {
            obj->expire_at = expire_at;
            db.Set(key, std::move(obj));
        }
    }

    std::cout << "[RDB] Loaded " << db.Size() << " keys from " << path_ << std::endl;
    return true;
}

}  // namespace redis
