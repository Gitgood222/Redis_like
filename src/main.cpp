#include "server.h"
#include <iostream>
#include <csignal>
#include <atomic>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace {

std::atomic<bool> g_stop{false};

#ifdef _WIN32
BOOL WINAPI WinCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}
#else
void SigHandler(int) { g_stop.store(true); }
#endif

void SetupSignalHandlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(WinCtrlHandler, TRUE);
#else
    signal(SIGINT, SigHandler);
    signal(SIGTERM, SigHandler);
#endif
}

void SleepMs(int ms) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(ms));
#else
    usleep(ms * 1000);
#endif
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    int port = redis::kDefaultPort;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    SetupSignalHandlers();

    std::cout << "redis_like v1.0.0" << std::endl;
    std::cout << "Connect with: redis-cli -p " << port << std::endl;

    redis::RedisServer server;
    if (!server.Init(port)) {
        return 1;
    }

    while (!g_stop.load()) {
        server.Tick(100);
    }

    std::cout << "\n[INFO] Shutting down..." << std::endl;
    server.Stop();

    return 0;
}
