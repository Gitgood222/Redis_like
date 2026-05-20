#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    constexpr socket_t kInvalidSocket = INVALID_SOCKET;
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
    constexpr socket_t kInvalidSocket = -1;
    #define CLOSE_SOCKET close
#endif

using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::milliseconds;

// ---------- RESP helpers ----------

std::string BuildCommand(const std::string& name,
                          const std::vector<std::string>& args) {
    std::string r;
    r += '*';
    r += std::to_string(1 + args.size());
    r += "\r\n";
    r += '$';
    r += std::to_string(name.size());
    r += "\r\n";
    r += name;
    r += "\r\n";
    for (const auto& a : args) {
        r += '$';
        r += std::to_string(a.size());
        r += "\r\n";
        r += a;
        r += "\r\n";
    }
    return r;
}

// ---------- TCP client ----------

class BenchClient {
public:
    bool Connect(const std::string& host, int port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ == kInvalidSocket) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = inet_addr(host.c_str());

        if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            CLOSE_SOCKET(fd_);
            fd_ = kInvalidSocket;
            return false;
        }
        return true;
    }

    ~BenchClient() {
        if (fd_ != kInvalidSocket) CLOSE_SOCKET(fd_);
    }

    std::string Send(const std::string& req) {
        send(fd_, req.data(), static_cast<int>(req.size()), 0);
        char buf[8192]{};
        int n = recv(fd_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return {};
        return std::string(buf, n);
    }

private:
    socket_t fd_ = kInvalidSocket;
};

// ---------- benchmark runner ----------

struct BenchResult {
    std::string name;
    int         ops       = 0;
    double      durationMs = 0;
    double      qps       = 0;
    double      avgLatencyUs = 0;
};

BenchResult RunBench(const std::string& name,
                      const std::string& setupCmd,
                      const std::string& testCmd,
                      int iterations) {
    BenchClient cli;
    if (!cli.Connect("127.0.0.1", 6379)) {
        std::cerr << "[ERROR] Cannot connect. Is the server running?" << std::endl;
        return {name, 0, 0, 0, 0};
    }

    // Setup
    if (!setupCmd.empty()) cli.Send(setupCmd);

    // Warmup
    for (int i = 0; i < 100; ++i) cli.Send(testCmd);

    // Benchmark
    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        cli.Send(testCmd);
    }
    auto end = Clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    double qps = iterations / (ms / 1000.0);
    double avgUs = (ms * 1000.0) / iterations;

    return {name, iterations, ms, qps, avgUs};
}

// ---------- main ----------

int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    std::cout << "redis_like Benchmark" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << std::endl;
    std::cout << "Make sure the server is running on port 6379." << std::endl;
    std::cout << std::endl;

    struct Test {
        std::string name;
        std::string setup;
        std::string cmd;
        int         iters;
    };

    std::vector<Test> tests = {
        {"SET",         "",                 BuildCommand("SET", {"b:key", "val"}),     50000},
        {"GET",         BuildCommand("SET", {"b:get", "v"}), BuildCommand("GET", {"b:get"}), 50000},
        {"HSET",        "",                 BuildCommand("HSET", {"b:h", "f", "v"}),   30000},
        {"HGET",        BuildCommand("HSET", {"b:hg", "f", "v"}), BuildCommand("HGET", {"b:hg", "f"}), 30000},
        {"LPUSH",       "",                 BuildCommand("LPUSH", {"b:l", "x"}),       30000},
        {"LRANGE 0-9",  BuildCommand("RPUSH", {"b:lr", "a","b","c","d","e","f","g","h","i","j"}), BuildCommand("LRANGE", {"b:lr","0","9"}), 20000},
        {"SADD",        "",                 BuildCommand("SADD", {"b:s", "m"}),         30000},
        {"ZADD",        "",                 BuildCommand("ZADD", {"b:z", "1", "m"}),   20000},
    };

    std::vector<BenchResult> results;

    for (const auto& t : tests) {
        std::cout << "  Benchmarking " << t.name << " (" << t.iters << " iterations) ... ";
        std::cout.flush();
        auto r = RunBench(t.name, t.setup, t.cmd, t.iters);
        results.push_back(r);
        std::cout << r.qps << " ops/sec" << std::endl;
    }

    // Summary table
    std::cout << std::endl;
    std::cout << "Summary" << std::endl;
    std::cout << "-------" << std::endl;
    std::cout << std::endl;
    printf("%-15s %8s %10s %12s\n", "Operation", "Ops", "QPS", "Avg Latency");
    printf("%-15s %8s %10s %12s\n", "---------", "---", "---", "-----------");

    for (const auto& r : results) {
        printf("%-15s %8d %9.0f %8.1f us\n",
               r.name.c_str(), r.ops, r.qps, r.avgLatencyUs);
    }

    std::cout << std::endl;
    std::cout << "Note: Single-threaded event loop. QPS limited by network RTT." << std::endl;

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
