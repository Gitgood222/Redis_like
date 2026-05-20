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
        RegisterZSetCommands(router);
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

// ---------- ZADD / ZSCORE ----------

TEST(zadd_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("ZADD", {"z", "1.5", "a"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("ZADD", {"z", "2.0", "b"}), ":1\r\n");
    // update existing → 0
    ASSERT_EQ(fx.Exec("ZADD", {"z", "3.0", "a"}), ":0\r\n");
    // mix new + existing
    ASSERT_EQ(fx.Exec("ZADD", {"z", "4.0", "b", "5.0", "c"}), ":1\r\n");
}

TEST(zscore_basic) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "3.14", "pi"});
    auto r = fx.Exec("ZSCORE", {"z", "pi"});
    ASSERT_TRUE(r.find("3.14") != std::string::npos);
}

TEST(zscore_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("ZSCORE", {"z", "x"}), "$-1\r\n");
}

// ---------- ZREM ----------

TEST(zrem_basic) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "1", "a", "2", "b", "3", "c"});
    ASSERT_EQ(fx.Exec("ZREM", {"z", "a", "c", "x"}), ":2\r\n");
    ASSERT_EQ(fx.Exec("ZSCORE", {"z", "a"}), "$-1\r\n");
    ASSERT_EQ(fx.Exec("ZSCORE", {"z", "b"}), "$1\r\n2\r\n");  // score 2
}

TEST(zrem_cleans_empty) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "1", "a"});
    fx.Exec("ZREM", {"z", "a"});
    ASSERT_EQ(fx.Exec("EXISTS", {"z"}), ":0\r\n");
}

// ---------- ZRANK / ZREVRANK ----------

TEST(zrank_basic) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "10", "x", "20", "y", "30", "z"});
    ASSERT_EQ(fx.Exec("ZRANK",  {"z", "x"}), ":0\r\n");
    ASSERT_EQ(fx.Exec("ZRANK",  {"z", "y"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("ZRANK",  {"z", "z"}), ":2\r\n");
}

TEST(zrevrank_basic) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "10", "x", "20", "y", "30", "z"});
    ASSERT_EQ(fx.Exec("ZREVRANK", {"z", "x"}), ":2\r\n");
    ASSERT_EQ(fx.Exec("ZREVRANK", {"z", "y"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("ZREVRANK", {"z", "z"}), ":0\r\n");
}

TEST(zrank_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("ZRANK", {"z", "x"}), "$-1\r\n");
}

// ---------- ZRANGE ----------

TEST(zrange_basic) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "10", "c", "20", "a", "30", "b"});
    // order by score: c(10), a(20), b(30)
    auto r = fx.Exec("ZRANGE", {"z", "0", "-1"});
    ASSERT_TRUE(r.find("c") != std::string::npos);
    ASSERT_TRUE(r.find("a") != std::string::npos);
    ASSERT_TRUE(r.find("b") != std::string::npos);
    // check order: c comes before a before b
    size_t pc = r.find("c"), pa = r.find("a"), pb = r.find("b");
    ASSERT_TRUE(pc < pa && pa < pb);
}

TEST(zrange_withscores) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "10", "a", "20", "b"});
    auto r = fx.Exec("ZRANGE", {"z", "0", "-1", "WITHSCORES"});
    ASSERT_TRUE(r.find("10") != std::string::npos);
    ASSERT_TRUE(r.find("20") != std::string::npos);
}

TEST(zrange_empty) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("ZRANGE", {"z", "0", "-1"}), "*0\r\n");
}

// ---------- ZREVRANGE ----------

TEST(zrevrange_basic) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "10", "c", "20", "a", "30", "b"});
    // reverse: b(30), a(20), c(10)
    auto r = fx.Exec("ZREVRANGE", {"z", "0", "-1"});
    size_t pb = r.find("b"), pa = r.find("a"), pc = r.find("c");
    ASSERT_TRUE(pb < pa && pa < pc);
}

TEST(zrevrange_withscores) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "10", "a", "20", "b"});
    auto r = fx.Exec("ZREVRANGE", {"z", "0", "1", "WITHSCORES"});
    ASSERT_TRUE(r.find("20") != std::string::npos); // b,20 first
    ASSERT_TRUE(r.find("10") != std::string::npos); // a,10 second
}

// ---------- ZCARD ----------

TEST(zcard_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("ZCARD", {"z"}), ":0\r\n");
    fx.Exec("ZADD", {"z", "1", "a", "2", "b"});
    ASSERT_EQ(fx.Exec("ZCARD", {"z"}), ":2\r\n");
}

// ---------- WRONGTYPE ----------

TEST(zset_wrongtype) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    auto r = fx.Exec("ZADD", {"k", "1", "x"});
    ASSERT_TRUE(r.find("WRONGTYPE") != std::string::npos);
}

// ---------- duplicate member update ----------

TEST(zadd_update_score) {
    TestFixture fx;
    fx.Exec("ZADD", {"z", "100", "m"});
    ASSERT_EQ(fx.Exec("ZSCORE", {"z", "m"}), "$3\r\n100\r\n");
    fx.Exec("ZADD", {"z", "50", "m"});
    ASSERT_EQ(fx.Exec("ZSCORE", {"z", "m"}), "$2\r\n50\r\n");
    ASSERT_EQ(fx.Exec("ZCARD", {"z"}), ":1\r\n");
}

// ---------- main ----------

int main() {
    std::cout << "=== ZSet Command Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
