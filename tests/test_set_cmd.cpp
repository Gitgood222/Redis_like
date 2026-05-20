#include "command/router.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

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
        RegisterSetCommands(router);
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

// ---------- SADD / SMEMBERS ----------

TEST(sadd_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SADD", {"s", "a"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("SADD", {"s", "b", "c"}), ":2\r\n");
    // duplicates
    ASSERT_EQ(fx.Exec("SADD", {"s", "a"}), ":0\r\n");
}

TEST(smembers_basic) {
    TestFixture fx;
    fx.Exec("SADD", {"s", "x", "y", "z"});
    auto r = fx.Exec("SMEMBERS", {"s"});
    ASSERT_TRUE(r.find("x") != std::string::npos);
    ASSERT_TRUE(r.find("y") != std::string::npos);
    ASSERT_TRUE(r.find("z") != std::string::npos);
}

TEST(smembers_empty) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SMEMBERS", {"no"}), "*0\r\n");
}

// ---------- SREM ----------

TEST(srem_basic) {
    TestFixture fx;
    fx.Exec("SADD", {"s", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("SREM", {"s", "a", "d"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("SISMEMBER", {"s", "a"}), ":0\r\n");
    ASSERT_EQ(fx.Exec("SISMEMBER", {"s", "b"}), ":1\r\n");
}

TEST(srem_cleans_empty) {
    TestFixture fx;
    fx.Exec("SADD", {"s", "a"});
    fx.Exec("SREM", {"s", "a"});
    ASSERT_EQ(fx.Exec("EXISTS", {"s"}), ":0\r\n");
}

// ---------- SISMEMBER ----------

TEST(sismember_basic) {
    TestFixture fx;
    fx.Exec("SADD", {"s", "hello"});
    ASSERT_EQ(fx.Exec("SISMEMBER", {"s", "hello"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("SISMEMBER", {"s", "world"}), ":0\r\n");
    ASSERT_EQ(fx.Exec("SISMEMBER", {"no", "hello"}), ":0\r\n");
}

// ---------- SCARD ----------

TEST(scard_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SCARD", {"s"}), ":0\r\n");
    fx.Exec("SADD", {"s", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("SCARD", {"s"}), ":3\r\n");
}

// ---------- SPOP ----------

TEST(spop_basic) {
    TestFixture fx;
    fx.Exec("SADD", {"s", "x"});
    auto r = fx.Exec("SPOP", {"s"});
    ASSERT_TRUE(r.find("x") != std::string::npos);
    ASSERT_EQ(fx.Exec("EXISTS", {"s"}), ":0\r\n");
}

TEST(spop_empty) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SPOP", {"no"}), "$-1\r\n");
}

TEST(spop_count) {
    TestFixture fx;
    fx.Exec("SADD", {"s", "a", "b", "c", "d"});
    auto r = fx.Exec("SPOP", {"s", "2"});
    ASSERT_TRUE(r.find("*2") == 0);
    ASSERT_EQ(fx.Exec("SCARD", {"s"}), ":2\r\n");
}

// ---------- WRONGTYPE ----------

TEST(set_wrongtype) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    auto r = fx.Exec("SADD", {"k", "x"});
    ASSERT_TRUE(r.find("WRONGTYPE") != std::string::npos);
    ASSERT_EQ(fx.Exec("SISMEMBER", {"k", "x"}), ":0\r\n");
}

// ---------- main ----------

int main() {
    std::cout << "=== Set Command Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
