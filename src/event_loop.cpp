#include "event_loop.h"
#include <algorithm>
#include <cerrno>
#include <iostream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cstring>
#endif

namespace redis {

// ---------- socket helpers ----------

bool SetNonBlocking(socket_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

#ifdef _WIN32
static bool winsock_initialized = false;

static void InitWinsock() {
    if (winsock_initialized) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    winsock_initialized = true;
}
#endif

socket_t CreateListenSocket(int port, int backlog) {
#ifdef _WIN32
    InitWinsock();
#endif

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) return kInvalidSocket;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
               &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        CLOSE_SOCKET(fd);
        return kInvalidSocket;
    }

    if (listen(fd, backlog) < 0) {
        CLOSE_SOCKET(fd);
        return kInvalidSocket;
    }

    SetNonBlocking(fd);
    return fd;
}

// ---------- EventLoop ----------

EventLoop::EventLoop() {}

EventLoop::~EventLoop() {
#ifdef __linux__
    if (epfd_ != -1) close(epfd_);
#endif
}

void EventLoop::AddEvent(socket_t fd, int mask, EventCallback cb) {
    auto& ev = events_[fd];
    ev.fd   = fd;
    ev.mask = mask;
    ev.cb   = std::move(cb);
}

void EventLoop::RemoveEvent(socket_t fd) {
    events_.erase(fd);
}

void EventLoop::ModifyMask(socket_t fd, int mask) {
    auto it = events_.find(fd);
    if (it != events_.end()) it->second.mask = mask;
}

void EventLoop::FdSet(socket_t fd, fd_set* set, socket_t& maxfd) {
#ifdef _WIN32
    FD_SET(fd, set);
#else
    FD_SET(fd, set);
#endif
    if (fd > maxfd) maxfd = fd;
}

void EventLoop::Dispatch(socket_t fd, int mask) {
    auto it = events_.find(fd);
    if (it != events_.end() && it->second.cb) {
        it->second.cb(mask);
    }
}

void EventLoop::RunOnce(int timeoutMs) {
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    socket_t maxfd = -1;

    for (const auto& [fd, ev] : events_) {
        if (ev.mask & kEventReadable) FdSet(fd, &rfds, maxfd);
        if (ev.mask & kEventWritable) FdSet(fd, &wfds, maxfd);
    }

    if (maxfd == -1) return; // no events registered

    timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int n = select(static_cast<int>(maxfd) + 1, &rfds, &wfds, nullptr, &tv);
    if (n <= 0) return;

    for (const auto& [fd, ev] : events_) {
        int mask = 0;
        if (FD_ISSET(fd, &rfds)) mask |= kEventReadable;
        if (FD_ISSET(fd, &wfds)) mask |= kEventWritable;
        if (mask) Dispatch(fd, mask);
    }
}

void EventLoop::Run() {
    running_ = true;
    while (running_) {
        RunOnce(100);
    }
}

}  // namespace redis
