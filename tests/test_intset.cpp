#include "ds/intset.h"
#include <cassert>
#include <iostream>
#include <string>
#include <stdexcept>

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

// ---------- tests ----------

TEST(empty_on_create) {
    IntSet s;
    ASSERT_EQ(s.Size(), static_cast<size_t>(0));
    ASSERT_TRUE(s.GetAll().empty());
}

TEST(add_and_contains) {
    IntSet s;
    s.Add(10);
    s.Add(5);
    s.Add(20);

    ASSERT_EQ(s.Size(), static_cast<size_t>(3));
    ASSERT_TRUE(s.Contains(5));
    ASSERT_TRUE(s.Contains(10));
    ASSERT_TRUE(s.Contains(20));
    ASSERT_TRUE(!s.Contains(0));
    ASSERT_TRUE(!s.Contains(100));
}

TEST(duplicate_add_ignored) {
    IntSet s;
    s.Add(42);
    s.Add(42);
    s.Add(42);
    ASSERT_EQ(s.Size(), static_cast<size_t>(1));
    ASSERT_TRUE(s.Contains(42));
}

TEST(remove_existing) {
    IntSet s;
    s.Add(1);
    s.Add(2);
    s.Add(3);
    ASSERT_EQ(s.Size(), static_cast<size_t>(3));

    s.Remove(2);
    ASSERT_EQ(s.Size(), static_cast<size_t>(2));
    ASSERT_TRUE(s.Contains(1));
    ASSERT_TRUE(!s.Contains(2));
    ASSERT_TRUE(s.Contains(3));
}

TEST(remove_nonexistent) {
    IntSet s;
    s.Add(1);
    s.Add(3);
    s.Remove(99);   // no-op
    ASSERT_EQ(s.Size(), static_cast<size_t>(2));
}

TEST(remove_all_elements) {
    IntSet s;
    s.Add(10);
    s.Remove(10);
    ASSERT_EQ(s.Size(), static_cast<size_t>(0));
    ASSERT_TRUE(!s.Contains(10));
}

TEST(int16_range) {
    IntSet s;
    s.Add(INT16_MAX);
    s.Add(INT16_MIN);
    s.Add(0);
    ASSERT_EQ(s.Size(), static_cast<size_t>(3));
    ASSERT_TRUE(s.Contains(INT16_MAX));
    ASSERT_TRUE(s.Contains(INT16_MIN));
    ASSERT_TRUE(s.Contains(0));
}

TEST(upgrade_to_int32) {
    IntSet s;
    s.Add(1);
    s.Add(2);
    // Value beyond int16 range triggers upgrade
    s.Add(static_cast<int64_t>(INT16_MAX) + 1);

    ASSERT_EQ(s.Size(), static_cast<size_t>(3));
    ASSERT_TRUE(s.Contains(1));
    ASSERT_TRUE(s.Contains(2));
    ASSERT_TRUE(s.Contains(static_cast<int64_t>(INT16_MAX) + 1));
}

TEST(upgrade_to_int64) {
    IntSet s;
    s.Add(1);
    s.Add(2);
    // Value beyond int32 range triggers upgrade
    s.Add(static_cast<int64_t>(INT32_MAX) + 1);

    ASSERT_EQ(s.Size(), static_cast<size_t>(3));
    ASSERT_TRUE(s.Contains(1));
    ASSERT_TRUE(s.Contains(2));
    ASSERT_TRUE(s.Contains(static_cast<int64_t>(INT32_MAX) + 1));
}

TEST(multiple_upgrades) {
    IntSet s;
    s.Add(100);                                        // int16
    s.Add(static_cast<int64_t>(INT16_MAX) + 10);      // upgrade to int32
    s.Add(static_cast<int64_t>(INT32_MAX) + 10);      // upgrade to int64
    s.Add(-500);

    ASSERT_EQ(s.Size(), static_cast<size_t>(4));
    ASSERT_TRUE(s.Contains(100));
    ASSERT_TRUE(s.Contains(static_cast<int64_t>(INT16_MAX) + 10));
    ASSERT_TRUE(s.Contains(static_cast<int64_t>(INT32_MAX) + 10));
    ASSERT_TRUE(s.Contains(-500));
}

TEST(sorted_order_maintained) {
    IntSet s;
    // Insert in random order
    s.Add(50);
    s.Add(10);
    s.Add(30);
    s.Add(20);
    s.Add(40);

    auto vals = s.GetAll();
    ASSERT_EQ(vals.size(), static_cast<size_t>(5));
    for (size_t i = 1; i < vals.size(); i++) {
        ASSERT_TRUE(vals[i - 1] <= vals[i]);
    }
    ASSERT_EQ(vals[0], static_cast<int64_t>(10));
    ASSERT_EQ(vals[4], static_cast<int64_t>(50));
}

TEST(sorted_after_upgrade) {
    IntSet s;
    s.Add(10);
    s.Add(static_cast<int64_t>(INT16_MAX) + 1);   // triggers upgrade
    s.Add(-5);
    s.Add(5);

    auto vals = s.GetAll();
    ASSERT_EQ(vals.size(), static_cast<size_t>(4));
    for (size_t i = 1; i < vals.size(); i++) {
        ASSERT_TRUE(vals[i - 1] <= vals[i]);
    }
}

TEST(contains_with_wider_type) {
    IntSet s;
    s.Add(1);
    s.Add(2);
    // Searching for a value that needs wider encoding returns false
    ASSERT_TRUE(!s.Contains(static_cast<int64_t>(INT16_MAX) + 100));
}

TEST(large_count) {
    IntSet s;
    const int N = 5000;
    for (int i = 0; i < N; i++) {
        s.Add(i);
    }
    ASSERT_EQ(s.Size(), static_cast<size_t>(N));
    ASSERT_TRUE(s.Contains(0));
    ASSERT_TRUE(s.Contains(N - 1));
    ASSERT_TRUE(s.Contains(N / 2));

    // Remove half
    for (int i = 0; i < N; i += 2) {
        s.Remove(i);
    }
    ASSERT_EQ(s.Size(), static_cast<size_t>(N / 2));
    ASSERT_TRUE(!s.Contains(0));
    ASSERT_TRUE(s.Contains(1));
}

int main() {
    std::cout << "test_intset" << std::endl;
    // Tests run via static registration
    if (tests_failed == 0) {
        std::cout << "All " << tests_run << " tests passed." << std::endl;
    } else {
        std::cout << tests_failed << " of " << tests_run << " tests FAILED." << std::endl;
    }
    return tests_failed;
}
