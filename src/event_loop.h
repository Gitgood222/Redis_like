#pragma once

#include "common.h"
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

// ---------- EventLoop ----------
// select-based implementation for portability; epoll specialization on Linux.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // non-copyable, movable
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Register / modify / remove a fd from the loop.
    void AddEvent(socket_t fd, int mask, EventCallback cb);
    void RemoveEvent(socket_t fd);
    void ModifyMask(socket_t fd, int mask);

    // Block until at least one event fires, then dispatch.
    void RunOnce(int timeoutMs = 100);

    // Loop until stop() is called.
    void Run();

    void Stop() { running_ = false; }

private:
    void FdSet(socket_t fd, fd_set* set, socket_t& maxfd);
    void Dispatch(socket_t fd, int mask);

    std::unordered_map<socket_t, FileEvent> events_;
    bool running_ = false;

#ifdef __linux__
    int epfd_ = -1;  // reserved for epoll
#endif
};

// ---------- socket helpers ----------
bool SetNonBlocking(socket_t fd);
socket_t CreateListenSocket(int port, int backlog = 128);

}  // namespace redis
