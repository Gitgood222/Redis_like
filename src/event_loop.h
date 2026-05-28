#pragma once

#include "common.h"
#include <cstdint>
#include <cstring>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    constexpr socket_t kInvalidSocket = INVALID_SOCKET;
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    using socket_t = int;
    constexpr socket_t kInvalidSocket = -1;
    #define CLOSE_SOCKET close
#endif

namespace redis {

// ---------- event types ----------
constexpr int kEventReadable = 1 << 0;
constexpr int kEventWritable = 1 << 1;

using EventCallback = std::function<void(int mask)>;

struct FileEvent {
    socket_t fd = kInvalidSocket;
    int      mask = 0;       // kEventReadable | kEventWritable
    EventCallback cb;
};

struct TimeEvent {
    int64_t            id;
    TimePoint          when;    // absolute fire time
    Duration           period;  // 0 = one-shot, >0 = recurring interval
    std::function<void()> callback;
};

// ---------- EventLoop ----------
// Uses epoll on Linux, select on other platforms.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // non-copyable, movable
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // File events — register / modify / remove a fd from the loop.
    void AddEvent(socket_t fd, int mask, EventCallback cb);
    void RemoveEvent(socket_t fd);
    void ModifyMask(socket_t fd, int mask);

    // Time events — returns an id for later removal.
    // period = 0 for one-shot, >0 for recurring.
    int64_t AddTimeEvent(Duration after, Duration period,
                         std::function<void()> callback);
    void RemoveTimeEvent(int64_t id);

    // Block until at least one event fires, then dispatch.
    void RunOnce(int timeoutMs = 100);

    // Loop until stop() is called.
    void Run();

    void Stop() { running_ = false; }

private:
    void FdSet(socket_t fd, fd_set* set, socket_t& maxfd);
    void Dispatch(socket_t fd, int mask);
    void ProcessTimeEvents();

    std::unordered_map<socket_t, FileEvent> events_;
    bool running_ = false;

    std::vector<TimeEvent> time_events_;
    int64_t next_time_event_id_ = 1;

#ifdef __linux__
    int epfd_ = -1;  // reserved for epoll
#endif
};

// ---------- socket helpers ----------
bool SetNonBlocking(socket_t fd);
socket_t CreateListenSocket(int port, int backlog = 128);

}  // namespace redis
