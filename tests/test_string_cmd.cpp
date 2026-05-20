#include "command/router.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace redis;

// ---------- minimal test framework ----------
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

// ---------- test fixture ----------
struct TestFixture {
    Dict          db;
    ExpireManager expire;
    CommandRouter router;

    TestFixture() {
        RegisterStringCommands(router);
        RegisterKeyCommands(router);
    }

    // Create a command from a name and args list.
    RespCommand Cmd(const std::string& name,
                    const std::vector<std::string>& args = {}) {
        RespCommand c;
        c.name = name;
        c.args = args;
        return c;
    }

    // Execute a command, return the response string.
    std::string Exec(const std::string& name,
                     const std::vector<std::string>& args = {}) {
        CmdContext ctx{db, expire, Cmd(name, args), Clock::now()};
        return router.Execute(ctx);
    }
};

// ---------- SET / GET tests ----------

TEST(set_get_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SET", {"key1", "hello"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"key1"}), "$5\r\nhello\r\n");
}

TEST(get_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("GET", {"no_key"}), "$-1\r\n");
}

TEST(set_overwrite) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v1"});
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$2\r\nv1\r\n");
    fx.Exec("SET", {"k", "v2"});
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$2\r\nv2\r\n");
}

TEST(set_nx) {
    TestFixture fx;
    // NX on non-existent key → success
    ASSERT_EQ(fx.Exec("SET", {"k", "v", "NX"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$1\r\nv\r\n");
    // NX on existing key → fail (null)
    ASSERT_EQ(fx.Exec("SET", {"k", "v2", "NX"}), "$-1\r\n");
    // value unchanged
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$1\r\nv\r\n");
}

TEST(set_xx) {
    TestFixture fx;
    // XX on non-existent key → fail
    ASSERT_EQ(fx.Exec("SET", {"k", "v", "XX"}), "$-1\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$-1\r\n");
    // XX on existing key → success
    fx.Exec("SET", {"k", "v1"});
    ASSERT_EQ(fx.Exec("SET", {"k", "v2", "XX"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$2\r\nv2\r\n");
}

TEST(set_ex) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SET", {"k", "v", "EX", "10"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$1\r\nv\r\n");
    auto ttl = fx.Exec("TTL", {"k"});
    // TTL floors to seconds, so may be 9 or 10 due to timing
    ASSERT_TRUE(ttl == ":10\r\n" || ttl == ":9\r\n");
}

TEST(set_px) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SET", {"k", "v", "PX", "5000"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$1\r\nv\r\n");
    auto resp = fx.Exec("PTTL", {"k"});
    // PTTL returns milliseconds, should be close to 5000
    // format: :<integer>\r\n
    ASSERT_TRUE(resp.size() >= 4 && resp[0] == ':');
    int64_t ms = std::strtoll(resp.c_str() + 1, nullptr, 10);
    ASSERT_TRUE(ms >= 4000 && ms <= 5000);
}

TEST(set_nx_xx_mutex) {
    TestFixture fx;
    auto r = fx.Exec("SET", {"k", "v", "NX", "XX"});
    ASSERT_TRUE(r.find("ERR") != std::string::npos);
}

// ---------- DEL tests ----------

TEST(del_single) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    ASSERT_EQ(fx.Exec("DEL", {"k"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$-1\r\n");
}

TEST(del_multiple) {
    TestFixture fx;
    fx.Exec("SET", {"a", "1"});
    fx.Exec("SET", {"b", "2"});
    fx.Exec("SET", {"c", "3"});
    ASSERT_EQ(fx.Exec("DEL", {"a", "b", "x"}), ":2\r\n");
    ASSERT_EQ(fx.Exec("GET", {"a"}), "$-1\r\n");
    ASSERT_EQ(fx.Exec("GET", {"b"}), "$-1\r\n");
    ASSERT_EQ(fx.Exec("GET", {"c"}), "$1\r\n3\r\n");
}

TEST(del_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("DEL", {"no"}), ":0\r\n");
}

// ---------- EXISTS tests ----------

TEST(exists_basic) {
    TestFixture fx;
    fx.Exec("SET", {"a", "1"});
    fx.Exec("SET", {"b", "2"});
    ASSERT_EQ(fx.Exec("EXISTS", {"a"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("EXISTS", {"a", "b", "c"}), ":2\r\n");
    ASSERT_EQ(fx.Exec("EXISTS", {"c"}), ":0\r\n");
}

// ---------- EXPIRE / TTL tests ----------

TEST(expire_basic) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    ASSERT_EQ(fx.Exec("EXPIRE", {"k", "10"}), ":1\r\n");
    auto ttl = fx.Exec("TTL", {"k"});
    ASSERT_TRUE(ttl == ":10\r\n" || ttl == ":9\r\n");
}

TEST(expire_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("EXPIRE", {"no", "10"}), ":0\r\n");
}

TEST(expire_negative_deletes) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    ASSERT_EQ(fx.Exec("EXPIRE", {"k", "-1"}), ":1\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$-1\r\n");
}

TEST(ttl_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("TTL", {"no"}), ":-2\r\n");
}

TEST(ttl_no_expire) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    ASSERT_EQ(fx.Exec("TTL", {"k"}), ":-1\r\n");
}

TEST(pexpire_basic) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    ASSERT_EQ(fx.Exec("PEXPIRE", {"k", "5000"}), ":1\r\n");
    auto resp = fx.Exec("PTTL", {"k"});
    ASSERT_TRUE(resp.size() >= 4 && resp[0] == ':');
    int64_t ms = std::strtoll(resp.c_str() + 1, nullptr, 10);
    ASSERT_TRUE(ms >= 4000 && ms <= 5000);
}

TEST(pttl_nonexistent) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("PTTL", {"no"}), ":-2\r\n");
}

// ---------- TYPE tests ----------

TEST(type_string) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    ASSERT_EQ(fx.Exec("TYPE", {"k"}), "+string\r\n");
}

TEST(type_none) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("TYPE", {"no"}), "+none\r\n");
}

// ---------- EXPIRED key behavior ----------

TEST(get_expired_key) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    // Set expire to immediate past
    auto obj = fx.db.Get("k");
    obj->expire_at = Clock::now() - std::chrono::seconds(1);
    // GET triggers lazy check
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$-1\r\n");
}

TEST(exists_expired_key) {
    TestFixture fx;
    fx.Exec("SET", {"k", "v"});
    auto obj = fx.db.Get("k");
    obj->expire_at = Clock::now() - std::chrono::seconds(1);
    ASSERT_EQ(fx.Exec("EXISTS", {"k"}), ":0\r\n");
}

// ---------- main ----------

int main() {
    std::cout << "=== String / Key Command Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
