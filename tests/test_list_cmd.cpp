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
        RegisterListCommands(router);
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

// ---------- LPUSH / RPUSH ----------

TEST(lpush_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("LPUSH", {"l", "a"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("LPUSH", {"l", "b"}), ":2\r\n");
    // list is now: [b, a]
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "0", "-1"}), "*2\r\n$1\r\nb\r\n$1\r\na\r\n");
}

TEST(rpush_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("RPUSH", {"l", "a"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("RPUSH", {"l", "b"}), ":2\r\n");
    // list is now: [a, b]
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "0", "-1"}), "*2\r\n$1\r\na\r\n$1\r\nb\r\n");
}

TEST(lpush_multiple) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("LPUSH", {"l", "a", "b", "c"}), ":3\r\n");
    // pushed left-to-right: a(1) b(2) c(3) → [c, b, a]
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "0", "-1"}), "*3\r\n$1\r\nc\r\n$1\r\nb\r\n$1\r\na\r\n");
}

TEST(rpush_multiple) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("RPUSH", {"l", "a", "b", "c"}), ":3\r\n");
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "0", "-1"}), "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
}

// ---------- LPOP / RPOP ----------

TEST(lpop_basic) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("LPOP", {"l"}), "$1\r\na\r\n");
    ASSERT_EQ(fx.Exec("LPOP", {"l"}), "$1\r\nb\r\n");
    ASSERT_EQ(fx.Exec("LPOP", {"l"}), "$1\r\nc\r\n");
    ASSERT_EQ(fx.Exec("LPOP", {"l"}), "$-1\r\n");
}

TEST(rpop_basic) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("RPOP", {"l"}), "$1\r\nc\r\n");
    ASSERT_EQ(fx.Exec("RPOP", {"l"}), "$1\r\nb\r\n");
    ASSERT_EQ(fx.Exec("RPOP", {"l"}), "$1\r\na\r\n");
}

TEST(lpop_with_count) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c", "d"});
    auto r = fx.Exec("LPOP", {"l", "2"});
    ASSERT_TRUE(r.find("*2") == 0);           // array of 2
    ASSERT_TRUE(r.find("a") != std::string::npos);
    ASSERT_TRUE(r.find("b") != std::string::npos);
    ASSERT_EQ(fx.Exec("LLEN", {"l"}), ":2\r\n");
}

TEST(rpop_with_count) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c", "d"});
    auto r = fx.Exec("RPOP", {"l", "3"});
    ASSERT_TRUE(r.find("*3") == 0);
    ASSERT_TRUE(r.find("d") != std::string::npos);
    ASSERT_TRUE(r.find("c") != std::string::npos);
    ASSERT_TRUE(r.find("b") != std::string::npos);
}

TEST(lpop_cleans_empty_key) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a"});
    fx.Exec("LPOP", {"l"});
    ASSERT_EQ(fx.Exec("EXISTS", {"l"}), ":0\r\n");
}

// ---------- LLEN ----------

TEST(llen_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("LLEN", {"l"}), ":0\r\n");
    fx.Exec("RPUSH", {"l", "a", "b"});
    ASSERT_EQ(fx.Exec("LLEN", {"l"}), ":2\r\n");
}

// ---------- LRANGE ----------

TEST(lrange_full) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "0", "-1"}),
              "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
}

TEST(lrange_subrange) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c", "d"});
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "1", "2"}),
              "*2\r\n$1\r\nb\r\n$1\r\nc\r\n");
}

TEST(lrange_negative_index) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c"});
    // -2 to -1 → [b, c]
    ASSERT_EQ(fx.Exec("LRANGE", {"l", "-2", "-1"}),
              "*2\r\n$1\r\nb\r\n$1\r\nc\r\n");
}

TEST(lrange_empty_key) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("LRANGE", {"no", "0", "-1"}), "*0\r\n");
}

// ---------- LINDEX ----------

TEST(lindex_basic) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "0"}), "$1\r\na\r\n");
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "1"}), "$1\r\nb\r\n");
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "2"}), "$1\r\nc\r\n");
}

TEST(lindex_negative) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a", "b", "c"});
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "-1"}), "$1\r\nc\r\n");
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "-2"}), "$1\r\nb\r\n");
}

TEST(lindex_out_of_range) {
    TestFixture fx;
    fx.Exec("RPUSH", {"l", "a"});
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "5"}), "$-1\r\n");
    ASSERT_EQ(fx.Exec("LINDEX", {"l", "-3"}), "$-1\r\n");
}

TEST(lindex_empty_key) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("LINDEX", {"no", "0"}), "$-1\r\n");
}

// ---------- WRONGTYPE ----------

TEST(list_wrongtype) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    auto r = fx.Exec("LPUSH", {"k", "x"});
    ASSERT_TRUE(r.find("WRONGTYPE") != std::string::npos);
}

// ---------- main ----------

int main() {
    std::cout << "=== List Command Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
