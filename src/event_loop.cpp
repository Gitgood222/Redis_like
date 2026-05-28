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
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cstring>
    #ifdef __linux__
        #include <sys/epoll.h>
    #else
        #include <sys/select.h>
    #endif
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

EventLoop::EventLoop() {
#ifdef __linux__
    epfd_ = epoll_create1(0);
    if (epfd_ == -1) {
        std::cerr << "[ERROR] epoll_create1 failed: " << std::strerror(errno) << std::endl;
    }
#endif
}

EventLoop::~EventLoop() {
#ifdef __linux__
    if (epfd_ != -1) close(epfd_);
#endif
}

void EventLoop::AddEvent(socket_t fd, int mask, EventCallback cb) {
    bool exists = events_.find(fd) != events_.end();

    auto& ev = events_[fd];
    ev.fd   = fd;
    ev.mask = mask;
    ev.cb   = std::move(cb);

#ifdef __linux__
    if (epfd_ == -1) return;

    epoll_event ee{};
    ee.data.fd = fd;
    if (mask & kEventReadable) ee.events |= EPOLLIN;
    if (mask & kEventWritable) ee.events |= EPOLLOUT;

    int op = exists ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(epfd_, op, fd, &ee) == -1) {
        std::cerr << "[ERROR] epoll_ctl add fd=" << fd << ": " << std::strerror(errno) << std::endl;
    }
#endif
}

void EventLoop::RemoveEvent(socket_t fd) {
#ifdef __linux__
    if (epfd_ != -1) {
        epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    }
#endif
    events_.erase(fd);
}

void EventLoop::ModifyMask(socket_t fd, int mask) {
    auto it = events_.find(fd);
    if (it != events_.end()) {
        it->second.mask = mask;
#ifdef __linux__
        if (epfd_ == -1) return;
        epoll_event ee{};
        ee.data.fd = fd;
        if (mask & kEventReadable) ee.events |= EPOLLIN;
        if (mask & kEventWritable) ee.events |= EPOLLOUT;
        epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ee);
#endif
    }
}

#ifndef __linux__
void EventLoop::FdSet(socket_t fd, fd_set* set, socket_t& maxfd) {
    FD_SET(fd, set);
    if (fd > maxfd) maxfd = fd;
}
#endif

void EventLoop::Dispatch(socket_t fd, int mask) {
    auto it = events_.find(fd);
    if (it != events_.end() && it->second.cb) {
        it->second.cb(mask);
    }
}

void EventLoop::RunOnce(int timeoutMs) {
#ifdef __linux__
    if (epfd_ == -1) return;

    constexpr int kMaxEvents = 128;
    epoll_event fired[kMaxEvents];
    int n = epoll_wait(epfd_, fired, kMaxEvents, timeoutMs);
    if (n <= 0) return;

    for (int i = 0; i < n; i++) {
        int mask = 0;
        uint32_t ev = fired[i].events;
        if (ev & (EPOLLIN | EPOLLHUP | EPOLLERR)) mask |= kEventReadable;
        if (ev & EPOLLOUT)                    mask |= kEventWritable;
        if (mask) Dispatch(fired[i].data.fd, mask);
    }
#else
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
#endif
}

void EventLoop::Run() {
    running_ = true;
    while (running_) {
        RunOnce(100);
    }
}

}  // namespace redis
