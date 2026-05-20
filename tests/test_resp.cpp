#include "resp_codec.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace redis;

// minimal test framework
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

// ---------- tests ----------

TEST(bulk_string_serialize) {
    auto r = RespCodec::BulkString("hello");
    ASSERT_EQ(r, "$5\r\nhello\r\n");
}

TEST(null_bulk_string) {
    ASSERT_EQ(RespCodec::NullBulkString(), "$-1\r\n");
}

TEST(simple_string) {
    ASSERT_EQ(RespCodec::Ok(), "+OK\r\n");
}

TEST(error_response) {
    ASSERT_EQ(RespCodec::Error("ERR something"), "-ERR something\r\n");
}

TEST(integer_response) {
    ASSERT_EQ(RespCodec::Integer(42), ":42\r\n");
    ASSERT_EQ(RespCodec::Integer(-1), ":-1\r\n");
}

TEST(pong_response) {
    ASSERT_EQ(RespCodec::Pong(), "$4\r\nPONG\r\n");
}

TEST(empty_array) {
    ASSERT_EQ(RespCodec::EmptyArray(), "*0\r\n");
}

TEST(null_array) {
    ASSERT_EQ(RespCodec::NullArray(), "*-1\r\n");
}

TEST(array_response) {
    std::vector<std::string> items = {
        RespCodec::BulkString("one"),
        RespCodec::Integer(2),
    };
    auto r = RespCodec::Array(items);
    ASSERT_EQ(r, "*2\r\n$3\r\none\r\n:2\r\n");
}

TEST(parse_ping) {
    RespCodec codec;
    auto cmds = codec.Feed("*1\r\n$4\r\nPING\r\n", 14);
    ASSERT_EQ(cmds.size(), 1u);
    ASSERT_EQ(cmds[0].name, "PING");
    ASSERT_EQ(cmds[0].args.size(), 0u);
}

TEST(parse_set_command) {
    RespCodec codec;
    auto cmds = codec.Feed("*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n", 34);
    ASSERT_EQ(cmds.size(), 1u);
    ASSERT_EQ(cmds[0].name, "SET");
    ASSERT_EQ(cmds[0].args.size(), 2u);
    ASSERT_EQ(cmds[0].args[0], "key");
    ASSERT_EQ(cmds[0].args[1], "value");
}

TEST(parse_pipeline) {
    RespCodec codec;
    auto cmds = codec.Feed(
        "*1\r\n$4\r\nPING\r\n"
        "*1\r\n$4\r\nPING\r\n", 28);
    ASSERT_EQ(cmds.size(), 2u);
    ASSERT_EQ(cmds[0].name, "PING");
    ASSERT_EQ(cmds[1].name, "PING");
}

TEST(parse_partial_feed) {
    RespCodec codec;
    // *3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n = 33 bytes total
    // Feed first 13 bytes: *3\r\n$3\r\nSET\r\n
    auto cmds1 = codec.Feed("*3\r\n$3\r\nSET\r\n", 13);
    ASSERT_EQ(cmds1.size(), 0u); // incomplete — need 2 more bulk strings

    // Feed the rest
    auto cmds2 = codec.Feed("$3\r\nkey\r\n$5\r\nvalue\r\n", 20);
    ASSERT_EQ(cmds2.size(), 1u);
    ASSERT_EQ(cmds2[0].name, "SET");
    ASSERT_EQ(cmds2[0].args[0], "key");
    ASSERT_EQ(cmds2[0].args[1], "value");
}

TEST(parse_empty_args) {
    RespCodec codec;
    auto cmds = codec.Feed("*1\r\n$0\r\n\r\n", 11);
    ASSERT_EQ(cmds.size(), 1u);
    ASSERT_EQ(cmds[0].name, "");
    ASSERT_EQ(cmds[0].args.size(), 0u);
}

// ---------- main ----------

int main() {
    std::cout << "=== RESP Codec Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
