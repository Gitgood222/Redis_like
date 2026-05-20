#include "command/router.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <algorithm>

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

struct TestFixture {
    Dict          db;
    ExpireManager expire;
    CommandRouter router;

    TestFixture() {
        RegisterStringCommands(router);
        RegisterHashCommands(router);
        RegisterKeyCommands(router);
    }

    std::string Exec(const std::string& name,
                     const std::vector<std::string>& args = {}) {
        RespCommand c;
        c.name = name;
        c.args = args;
        CmdContext ctx{db, expire, std::move(c), Clock::now()};
        return router.Execute(ctx);
    }
};

// ---------- HSET / HGET ----------

TEST(hset_hget_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("HSET", {"h", "f1", "v1"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "f1"}), "$2\r\nv1\r\n");
}

TEST(hset_multiple) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("HSET", {"h", "a", "1", "b", "2", "c", "3"}), ":3\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "a"}), "$1\r\n1\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "b"}), "$1\r\n2\r\n");
}

TEST(hset_update_existing) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "f", "old"});
    // update should return 0 (field already exists)
    ASSERT_EQ(fx.Exec("HSET", {"h", "f", "new"}), ":0\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "f"}), "$3\r\nnew\r\n");
}

TEST(hset_mixed) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "a", "1"});
    // "a" exists, "b" is new → added = 1
    ASSERT_EQ(fx.Exec("HSET", {"h", "a", "10", "b", "20"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "a"}), "$2\r\n10\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "b"}), "$2\r\n20\r\n");
}

TEST(hget_nonexistent_key) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("HGET", {"no", "f"}), "$-1\r\n");
}

TEST(hget_nonexistent_field) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "f", "v"});
    ASSERT_EQ(fx.Exec("HGET", {"h", "no"}), "$-1\r\n");
}

TEST(hset_wrong_arg_count) {
    TestFixture fx;
    auto r = fx.Exec("HSET", {"h", "f"});
    ASSERT_TRUE(r.find("ERR") != std::string::npos);
}

// ---------- HDEL ----------

TEST(hdel_basic) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "a", "1", "b", "2", "c", "3"});
    ASSERT_EQ(fx.Exec("HDEL", {"h", "a", "c"}), ":2\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "a"}), "$-1\r\n");
    ASSERT_EQ(fx.Exec("HGET", {"h", "b"}), "$1\r\n2\r\n");
}

TEST(hdel_last_field_cleans_key) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "f", "v"});
    fx.Exec("HDEL", {"h", "f"});
    ASSERT_EQ(fx.Exec("EXISTS", {"h"}), ":0\r\n");
}

// ---------- HEXISTS ----------

TEST(hexists_basic) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "f", "v"});
    ASSERT_EQ(fx.Exec("HEXISTS", {"h", "f"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("HEXISTS", {"h", "no"}), ":0\r\n");
    ASSERT_EQ(fx.Exec("HEXISTS", {"no", "f"}), ":0\r\n");
}

// ---------- HKEYS / HVALS / HGETALL ----------

TEST(hkeys_empty) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("HKEYS", {"no"}), "*0\r\n");
}

TEST(hkeys_hvals_hgetall) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "name", "Alice", "age", "30"});

    auto keys = fx.Exec("HKEYS", {"h"});
    ASSERT_TRUE(keys.find("name") != std::string::npos);
    ASSERT_TRUE(keys.find("age")  != std::string::npos);

    auto vals = fx.Exec("HVALS", {"h"});
    ASSERT_TRUE(vals.find("Alice") != std::string::npos);
    ASSERT_TRUE(vals.find("30")    != std::string::npos);

    auto all = fx.Exec("HGETALL", {"h"});
    ASSERT_TRUE(all.find("name")  != std::string::npos);
    ASSERT_TRUE(all.find("Alice") != std::string::npos);
    ASSERT_TRUE(all.find("age")   != std::string::npos);
    ASSERT_TRUE(all.find("30")    != std::string::npos);
}

// ---------- HLEN ----------

TEST(hlen_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("HLEN", {"h"}), ":0\r\n");
    fx.Exec("HSET", {"h", "a", "1", "b", "2"});
    ASSERT_EQ(fx.Exec("HLEN", {"h"}), ":2\r\n");
}

// ---------- HSTRLEN ----------

TEST(hstrlen_basic) {
    TestFixture fx;
    fx.Exec("HSET", {"h", "f", "hello"});
    ASSERT_EQ(fx.Exec("HSTRLEN", {"h", "f"}), ":5\r\n");
    ASSERT_EQ(fx.Exec("HSTRLEN", {"h", "no"}), ":0\r\n");
    ASSERT_EQ(fx.Exec("HSTRLEN", {"no", "f"}), ":0\r\n");
}

// ---------- WRONGTYPE ----------

TEST(hash_wrongtype) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    auto r = fx.Exec("HSET", {"k", "f", "v"});
    ASSERT_TRUE(r.find("WRONGTYPE") != std::string::npos);
    r = fx.Exec("HGET", {"k", "f"});
    ASSERT_EQ(r, "$-1\r\n");
}

// ---------- main ----------

int main() {
    std::cout << "=== Hash Command Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
