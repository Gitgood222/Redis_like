#include "expire.h"
#include "object.h"
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

// Helper: create a string key with an expire time in the past
void AddKey(Dict& db, const std::string& key, TimePoint expire) {
    auto obj = RedisObject::CreateString("val");
    obj->expire_at = expire;
    db.Set(key, obj);
}

// ---------- periodic check ----------

TEST(periodic_deletes_expired_keys) {
    Dict db;
    ExpireManager em;
    auto past = Clock::now() - std::chrono::seconds(10);

    for (int i = 0; i < 20; ++i) {
        AddKey(db, "k" + std::to_string(i), past);
    }
    ASSERT_EQ(static_cast<int>(db.Size()), 20);

    int deleted = em.PeriodicCheck(db, Clock::now(), 20);
    ASSERT_TRUE(deleted > 0);
    ASSERT_EQ(static_cast<int>(db.Size()), 20 - deleted);
}

TEST(periodic_leaves_non_expired_keys) {
    Dict db;
    ExpireManager em;
    auto future = Clock::now() + std::chrono::seconds(100);

    for (int i = 0; i < 10; ++i) {
        AddKey(db, "k" + std::to_string(i), future);
    }
    ASSERT_EQ(static_cast<int>(db.Size()), 10);

    int deleted = em.PeriodicCheck(db, Clock::now(), 20);
    ASSERT_EQ(deleted, 0);
    ASSERT_EQ(static_cast<int>(db.Size()), 10);
}

TEST(periodic_mixed_expired_and_fresh) {
    Dict db;
    ExpireManager em;
    auto past   = Clock::now() - std::chrono::seconds(10);
    auto future = Clock::now() + std::chrono::seconds(100);

    for (int i = 0; i < 5; ++i) AddKey(db, "old_" + std::to_string(i), past);
    for (int i = 0; i < 5; ++i) AddKey(db, "new_" + std::to_string(i), future);

    ASSERT_EQ(static_cast<int>(db.Size()), 10);

    int deleted = em.PeriodicCheck(db, Clock::now(), 20);
    ASSERT_EQ(deleted, 5);
    ASSERT_EQ(static_cast<int>(db.Size()), 5);

    // Verify only fresh keys remain
    ASSERT_TRUE(db.Exists("new_0"));
    ASSERT_TRUE(!db.Exists("old_0"));
}

TEST(periodic_empty_db) {
    Dict db;
    ExpireManager em;
    int deleted = em.PeriodicCheck(db, Clock::now(), 20);
    ASSERT_EQ(deleted, 0);
}

TEST(periodic_many_expired_triggers_loop) {
    Dict db;
    ExpireManager em;
    auto past = Clock::now() - std::chrono::seconds(10);

    // Put 50 expired keys — should trigger multiple rounds (>25% threshold)
    for (int i = 0; i < 50; ++i) {
        AddKey(db, "k" + std::to_string(i), past);
    }
    ASSERT_EQ(static_cast<int>(db.Size()), 50);

    int deleted = em.PeriodicCheck(db, Clock::now(), 10); // sample 10 per round
    ASSERT_TRUE(deleted > 10); // multiple rounds should delete more than 10
    ASSERT_EQ(static_cast<int>(db.Size()), 50 - deleted);
}

// ---------- lazy check ----------

TEST(lazy_check_deletes_expired) {
    Dict db;
    ExpireManager em;
    auto past = Clock::now() - std::chrono::seconds(10);
    AddKey(db, "k", past);
    ASSERT_EQ(static_cast<int>(db.Size()), 1);

    bool deleted = em.LazyCheck(db, "k", Clock::now());
    ASSERT_TRUE(deleted);
    ASSERT_EQ(static_cast<int>(db.Size()), 0);
}

TEST(lazy_check_ignores_fresh) {
    Dict db;
    ExpireManager em;
    auto future = Clock::now() + std::chrono::seconds(100);
    AddKey(db, "k", future);

    bool deleted = em.LazyCheck(db, "k", Clock::now());
    ASSERT_TRUE(!deleted);
    ASSERT_EQ(static_cast<int>(db.Size()), 1);
}

TEST(lazy_check_nonexistent) {
    Dict db;
    ExpireManager em;
    bool deleted = em.LazyCheck(db, "no", Clock::now());
    ASSERT_TRUE(!deleted);
}

// ---------- TTL through expire manager ----------

TEST(get_ttl_expired) {
    auto obj = RedisObject::CreateString("v");
    obj->expire_at = Clock::now() - std::chrono::seconds(5);
    ExpireManager em;
    ASSERT_EQ(em.GetTTL(obj, Clock::now()), -2);
}

TEST(get_ttl_no_expire) {
    auto obj = RedisObject::CreateString("v");
    ExpireManager em;
    ASSERT_EQ(em.GetTTL(obj, Clock::now()), -1);
}

// ---------- main ----------

int main() {
    std::cout << "=== Expire Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Results: " << (tests_run - tests_failed) << "/"
              << tests_run << " passed";

    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
