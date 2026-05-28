#include "pubsub.h"
#include <cassert>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

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

#define ASSERT_TRUE(x)                                        \
    do { if (!(x)) throw std::runtime_error(#x); } while (0)

#define ASSERT_EQ(a, b)                                                \
    do {                                                               \
        if ((a) != (b)) {                                              \
            throw std::runtime_error(                                  \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) \
                + " " #a " != " #b);                                   \
        }                                                              \
    } while (0)

// ---------- helpers ----------

struct PubSubFixture {
    PubSubManager mgr;
    // Helper: record published messages
    struct Delivery {
        socket_t fd;
        std::string channel;
        std::string message;
        std::string pattern;
    };
    std::vector<Delivery> deliveries;

    auto MakePushFn() {
        return [this](socket_t fd, const std::string& channel,
                      const std::string& message, const std::string& pattern) {
            deliveries.push_back({fd, channel, message, pattern});
        };
    }
};

// ---------- subscribe / unsubscribe ----------

TEST(subscribe_basic) {
    PubSubFixture fx;
    ASSERT_TRUE(!fx.mgr.IsSubscribed(1));

    fx.mgr.Subscribe(1, "news");
    ASSERT_TRUE(fx.mgr.IsSubscribed(1));
    ASSERT_EQ(fx.mgr.SubscriptionCount(1), 1u);
    ASSERT_EQ(fx.mgr.ChannelSubscriberCount("news"), 1u);

    fx.mgr.Unsubscribe(1, "news");
    ASSERT_TRUE(!fx.mgr.IsSubscribed(1));
    ASSERT_EQ(fx.mgr.ChannelSubscriberCount("news"), 0u);
}

TEST(subscribe_multiple_channels) {
    PubSubFixture fx;
    fx.mgr.Subscribe(1, "a");
    fx.mgr.Subscribe(1, "b");
    fx.mgr.Subscribe(1, "c");
    ASSERT_EQ(fx.mgr.SubscriptionCount(1), 3u);
    ASSERT_EQ(fx.mgr.GetClientChannels(1).size(), 3u);

    fx.mgr.Unsubscribe(1, "b");
    ASSERT_EQ(fx.mgr.SubscriptionCount(1), 2u);
    ASSERT_EQ(fx.mgr.GetClientChannels(1).size(), 2u);
}

TEST(subscribe_multiple_clients) {
    PubSubFixture fx;
    fx.mgr.Subscribe(10, "news");
    fx.mgr.Subscribe(20, "news");
    fx.mgr.Subscribe(30, "news");
    ASSERT_EQ(fx.mgr.ChannelSubscriberCount("news"), 3u);

    fx.mgr.Unsubscribe(10, "news");
    ASSERT_EQ(fx.mgr.ChannelSubscriberCount("news"), 2u);
}

TEST(unsubscribe_all) {
    PubSubFixture fx;
    fx.mgr.Subscribe(1, "a");
    fx.mgr.Subscribe(1, "b");
    fx.mgr.Subscribe(1, "c");
    ASSERT_EQ(fx.mgr.SubscriptionCount(1), 3u);

    fx.mgr.UnsubscribeAll(1);
    ASSERT_TRUE(!fx.mgr.IsSubscribed(1));
    ASSERT_EQ(fx.mgr.SubscriptionCount(1), 0u);
    ASSERT_EQ(fx.mgr.ChannelSubscriberCount("a"), 0u);
    ASSERT_EQ(fx.mgr.ChannelSubscriberCount("b"), 0u);
}

// ---------- publish ----------

TEST(publish_to_channel) {
    PubSubFixture fx;
    fx.mgr.Subscribe(10, "news");
    fx.mgr.Subscribe(20, "news");

    int count = fx.mgr.Publish("news", "hello", fx.MakePushFn());
    ASSERT_EQ(count, 2);
    ASSERT_EQ(fx.deliveries.size(), 2u);
    ASSERT_EQ(fx.deliveries[0].channel, "news");
    ASSERT_EQ(fx.deliveries[0].message, "hello");
    ASSERT_EQ(fx.deliveries[1].channel, "news");
}

TEST(publish_no_subscribers) {
    PubSubFixture fx;
    int count = fx.mgr.Publish("empty", "msg", fx.MakePushFn());
    ASSERT_EQ(count, 0);
    ASSERT_EQ(fx.deliveries.size(), 0u);
}

TEST(publish_different_channels) {
    PubSubFixture fx;
    fx.mgr.Subscribe(10, "ch1");
    fx.mgr.Subscribe(20, "ch2");

    int c1 = fx.mgr.Publish("ch1", "a", fx.MakePushFn());
    ASSERT_EQ(c1, 1);
    ASSERT_EQ(fx.deliveries[0].fd, 10);
}

// ---------- pattern subscribe ----------

TEST(psubscribe_basic) {
    PubSubFixture fx;
    ASSERT_TRUE(!fx.mgr.IsSubscribed(1));

    fx.mgr.PSubscribe(1, "news*");
    ASSERT_TRUE(fx.mgr.IsSubscribed(1));
    ASSERT_EQ(fx.mgr.GetClientPatterns(1).size(), 1u);

    fx.mgr.PUnsubscribe(1, "news*");
    ASSERT_TRUE(!fx.mgr.IsSubscribed(1));
}

TEST(psubscribe_multiple_patterns) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(1, "a*");
    fx.mgr.PSubscribe(1, "b*");
    ASSERT_EQ(fx.mgr.GetClientPatterns(1).size(), 2u);

    fx.mgr.PUnsubscribe(1, "a*");
    ASSERT_EQ(fx.mgr.GetClientPatterns(1).size(), 1u);
}

TEST(publish_matches_pattern) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(10, "news*");

    int count = fx.mgr.Publish("news24", "breaking", fx.MakePushFn());
    ASSERT_EQ(count, 1);
    ASSERT_EQ(fx.deliveries[0].pattern, "news*");
    ASSERT_EQ(fx.deliveries[0].channel, "news24");
    ASSERT_EQ(fx.deliveries[0].message, "breaking");
}

TEST(publish_channel_and_pattern) {
    PubSubFixture fx;
    fx.mgr.Subscribe(10, "news");     // direct
    fx.mgr.PSubscribe(20, "n*");      // pattern

    int count = fx.mgr.Publish("news", "msg", fx.MakePushFn());
    ASSERT_EQ(count, 2);  // both receive
    // fd 10 gets pattern="" (direct), fd 20 gets pattern="n*"
    bool got_direct = false, got_pattern = false;
    for (const auto& d : fx.deliveries) {
        if (d.fd == 10 && d.pattern.empty()) got_direct = true;
        if (d.fd == 20 && d.pattern == "n*") got_pattern = true;
    }
    ASSERT_TRUE(got_direct);
    ASSERT_TRUE(got_pattern);
}

// ---------- glob matching ----------

TEST(pattern_glob_star) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(10, "foo*");
    ASSERT_EQ(fx.mgr.Publish("foo", "a", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("foobar", "b", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("xfoo", "c", fx.MakePushFn()), 0);
}

TEST(pattern_glob_question) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(10, "h?llo");
    ASSERT_EQ(fx.mgr.Publish("hello", "a", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("hallo", "b", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("hllo", "c", fx.MakePushFn()), 0);
    ASSERT_EQ(fx.mgr.Publish("helloo", "d", fx.MakePushFn()), 0);
}

TEST(pattern_glob_brackets) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(10, "h[ae]llo");
    ASSERT_EQ(fx.mgr.Publish("hello", "a", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("hallo", "b", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("hullo", "c", fx.MakePushFn()), 0);
}

TEST(pattern_glob_negated_brackets) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(10, "h[^e]llo");
    ASSERT_EQ(fx.mgr.Publish("hello", "a", fx.MakePushFn()), 0);
    ASSERT_EQ(fx.mgr.Publish("hallo", "b", fx.MakePushFn()), 1);
}

TEST(pattern_glob_bracket_range) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(10, "h[a-z]llo");
    ASSERT_EQ(fx.mgr.Publish("hello", "a", fx.MakePushFn()), 1);
    ASSERT_EQ(fx.mgr.Publish("h9llo", "b", fx.MakePushFn()), 0);
}

// ---------- unsub pattern all ----------

TEST(punsubscribe_all) {
    PubSubFixture fx;
    fx.mgr.PSubscribe(1, "a*");
    fx.mgr.PSubscribe(1, "b*");
    ASSERT_EQ(fx.mgr.GetClientPatterns(1).size(), 2u);

    // Simulate punsubscribe without args by getting all patterns
    auto patterns = fx.mgr.GetClientPatterns(1);
    for (const auto& p : patterns) {
        fx.mgr.PUnsubscribe(1, p);
    }
    ASSERT_TRUE(!fx.mgr.IsSubscribed(1));
}

// ---------- subscription count (mixed) ----------

TEST(mixed_channel_and_pattern_count) {
    PubSubFixture fx;
    fx.mgr.Subscribe(1, "news");
    fx.mgr.PSubscribe(1, "n*");
    ASSERT_EQ(fx.mgr.SubscriptionCount(1), 2u);
    ASSERT_EQ(fx.mgr.GetClientChannels(1).size(), 1u);
    ASSERT_EQ(fx.mgr.GetClientPatterns(1).size(), 1u);
}

int main() {
    std::cout << "=== Pub/Sub Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "Result: " << (tests_run - tests_failed) << "/" << tests_run
              << " passed";
    if (tests_failed > 0) {
        std::cout << ", " << tests_failed << " FAILED";
    }
    std::cout << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
