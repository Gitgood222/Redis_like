#include "storage/rdb.h"
#include "storage/aof.h"
#include "ds/skiplist.h"
#include "command/router.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <cstdio>

using namespace redis;

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name)                                         \
    void name();                                           \
    struct Register_##name {                               \
        Register_##name() { RunTest(#name, name); }        \
    } register_##name;                                     \
    void name()

void RunTest(const char* name, void (*fn)()) {
    tests_run++;
    std::cout << "  [" << tests_run << "] " << name << " ... ";
    try {
        fn();
        std::cout << "OK" << std::endl;
    } catch (const std::exception& e) {
        tests_failed++;
        std::cout << "FAIL: " << e.what() << std::endl;
    } catch (...) {
        tests_failed++;
        std::cout << "FAIL (unknown)" << std::endl;
    }
}

#define ASSERT_EQ(a, b)                                                \
    do {                                                               \
        if ((a) != (b)) {                                              \
            throw std::runtime_error(                                  \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) \
                + " " #a " != " #b);                                   \
        }                                                              \
    } while (0)

#define ASSERT_TRUE(x)                                                 \
    do {                                                               \
        if (!(x)) {                                                    \
            throw std::runtime_error(                                  \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) \
                + " " #x " is false");                                 \
        }                                                              \
    } while (0)

const std::string kTestRdb = "test_dump.rdb";
const std::string kTestAof = "test_appendonly.aof";

// ---------- helpers ----------

struct Fixture {
    Dict db;
    ExpireManager expire;

    void AddString(const std::string& key, const std::string& val) {
        auto obj = RedisObject::CreateString(val);
        db.Set(key, obj);
    }

    void AddHash(const std::string& key,
                 const std::vector<std::pair<std::string, std::string>>& fields) {
        auto obj = RedisObject::CreateHash();
        auto* h = obj->As<RedisHash>();
        for (const auto& f : fields) (*h)[f.first] = f.second;
        db.Set(key, obj);
    }

    void AddList(const std::string& key,
                 const std::vector<std::string>& vals) {
        auto obj = RedisObject::CreateList();
        auto* l = obj->As<RedisList>();
        for (const auto& v : vals) l->push_back(v);
        db.Set(key, obj);
    }

    void AddSet(const std::string& key,
                const std::vector<std::string>& members) {
        auto obj = RedisObject::CreateSet();
        auto* s = obj->As<RedisSet>();
        for (const auto& m : members) s->insert(m);
        db.Set(key, obj);
    }

    void AddZSet(const std::string& key,
                 const std::vector<std::pair<double, std::string>>& items) {
        auto obj = RedisObject::CreateZSet();
        auto* sl = obj->As<std::shared_ptr<SkipList>>()->get();
        for (const auto& p : items) sl->Insert(p.first, p.second);
        db.Set(key, obj);
    }
};

// ---------- RDB tests ----------

TEST(rdb_save_load_string) {
    Fixture fx;
    fx.AddString("key1", "hello");
    fx.AddString("key2", "world");

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));
    ASSERT_EQ(loaded.Size(), 2u);

    auto obj = loaded.Get("key1");
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(*obj->As<RedisString>(), "hello");
}

TEST(rdb_save_load_hash) {
    Fixture fx;
    fx.AddHash("h", {{"name", "Alice"}, {"age", "30"}});

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));
    ASSERT_EQ(loaded.Size(), 1u);

    auto obj = loaded.Get("h");
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->type, ObjType::kHash);
    auto* h = obj->As<RedisHash>();
    ASSERT_EQ((*h)["name"], "Alice");
    ASSERT_EQ((*h)["age"], "30");
}

TEST(rdb_save_load_list) {
    Fixture fx;
    fx.AddList("l", {"a", "b", "c"});

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));
    ASSERT_EQ(loaded.Size(), 1u);

    auto obj = loaded.Get("l");
    auto* lst = obj->As<RedisList>();
    ASSERT_EQ(lst->size(), 3u);
    ASSERT_EQ((*lst)[0], "a");
    ASSERT_EQ((*lst)[2], "c");
}

TEST(rdb_save_load_set) {
    Fixture fx;
    fx.AddSet("s", {"x", "y", "z"});

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));
    ASSERT_EQ(loaded.Size(), 1u);

    auto obj = loaded.Get("s");
    auto* set = obj->As<RedisSet>();
    ASSERT_TRUE(set->count("x"));
    ASSERT_TRUE(set->count("y"));
}

TEST(rdb_save_load_zset) {
    Fixture fx;
    fx.AddZSet("z", {{1.5, "a"}, {2.0, "b"}, {0.5, "c"}});

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));
    ASSERT_EQ(loaded.Size(), 1u);

    auto obj = loaded.Get("z");
    auto* sl = obj->As<std::shared_ptr<SkipList>>()->get();
    ASSERT_EQ(sl->Size(), 3u);
    ASSERT_EQ(*sl->GetScore("a"), 1.5);
}

TEST(rdb_save_load_with_expiry) {
    Fixture fx;
    auto obj = RedisObject::CreateString("val");
    obj->expire_at = Clock::now() + std::chrono::seconds(60);
    fx.db.Set("k", obj);

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));

    auto loaded_obj = loaded.Get("k");
    ASSERT_TRUE(loaded_obj != nullptr);
    ASSERT_TRUE(loaded_obj->expire_at.has_value());
}

TEST(rdb_save_load_all_types) {
    Fixture fx;
    fx.AddString("str", "value");
    fx.AddHash("hash", {{"f", "v"}});
    fx.AddList("list", {"a"});
    fx.AddSet("set", {"m"});
    fx.AddZSet("zset", {{1.0, "a"}});

    RdbSaver saver(kTestRdb);
    ASSERT_TRUE(saver.Save(fx.db));

    Dict loaded;
    ASSERT_TRUE(saver.Load(loaded));
    ASSERT_EQ(loaded.Size(), 5u);
}

TEST(rdb_load_nonexistent_file) {
    Dict loaded;
    RdbSaver saver("nonexistent.rdb");
    bool ok = saver.Load(loaded);
    // Loading a non-existent file should return false but not crash
    ASSERT_TRUE(!ok);
    ASSERT_EQ(loaded.Size(), 0u);
}

// ---------- AOF tests ----------

TEST(aof_is_write_command) {
    ASSERT_TRUE(AofLogger::IsWriteCommand("SET"));
    ASSERT_TRUE(AofLogger::IsWriteCommand("HSET"));
    ASSERT_TRUE(AofLogger::IsWriteCommand("LPUSH"));
    ASSERT_TRUE(AofLogger::IsWriteCommand("SADD"));
    ASSERT_TRUE(AofLogger::IsWriteCommand("ZADD"));
    ASSERT_TRUE(!AofLogger::IsWriteCommand("GET"));
    ASSERT_TRUE(!AofLogger::IsWriteCommand("PING"));
    ASSERT_TRUE(!AofLogger::IsWriteCommand("HGET"));
}

TEST(aof_append_and_replay) {
    // Ensure clean file
    std::remove(kTestAof.c_str());

    AofLogger logger(kTestAof);
    ASSERT_TRUE(logger.Open());

    // Manually write some RESP commands
    logger.Append("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n");
    logger.Append("*4\r\n$4\r\nHSET\r\n$1\r\nh\r\n$1\r\nf\r\n$1\r\nv\r\n");
    logger.Close();

    // Now replay into a Dict
    Dict db;
    ExpireManager expire;
    CommandRouter router;
    RegisterStringCommands(router);
    RegisterKeyCommands(router);
    RegisterHashCommands(router);

    int replayed = logger.Replay([&](RespCommand& cmd) {
        CmdContext ctx{db, expire, std::move(cmd), Clock::now()};
        router.Execute(ctx);
    });

    ASSERT_TRUE(replayed > 0);
    ASSERT_EQ(db.Size(), 2u);
    ASSERT_TRUE(db.Exists("k"));
    ASSERT_TRUE(db.Exists("h"));
}

// ---------- cleanup ----------
// Note: test RDB/AOF files are left in the working directory.
// In production, cleanup would be done after each test.

// ---------- main ----------

int main() {
    std::cout << "=== Persistence Tests ===" << std::endl;
    std::cout << std::endl;

    // Clean up from previous runs
    std::remove(kTestRdb.c_str());
    std::remove(kTestAof.c_str());

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    // Clean up test files
    std::remove(kTestRdb.c_str());
    std::remove(kTestAof.c_str());

    return tests_failed > 0 ? 1 : 0;
}
