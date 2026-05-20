# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Configure (MinGW on Windows)
cd build && cmake .. -G "MinGW Makefiles"

# Build
cmake --build .

# Run server
./redis_like.exe [port]

# Run all tests
./tests/test_resp.exe
./tests/test_string_cmd.exe

# Or use ctest
cd build && ctest
```

Tests are standalone executables (no external test framework). Each `tests/test_*.cpp` compiles against the source files it depends on, declared in `tests/CMakeLists.txt`.

## Architecture

Single-threaded event-loop server modeled on Redis. The `EventLoop` wraps `select()` (cross-platform; epoll reserved for Linux via `#ifdef __linux__`).

**Request path:**
1. `main.cpp` → `RedisServer::Init()` creates a listening socket and registers it with the event loop, then loops on `Tick()`.
2. `EventLoop::RunOnce()` uses `select()` to multiplex between the listen fd and client fds.
3. On readable client fd, `OnClientEvent()` calls `recv()`, feeds bytes into `RespCodec::Feed()`, and dispatches each parsed `RespCommand`.
4. `CommandRouter::Execute()` looks up the command name (case-insensitive) in an `unordered_map<string, CmdHandler>` and invokes the handler.
5. The handler receives `CmdContext&` (references to `Dict`, `ExpireManager`, parsed `RespCommand`, `TimePoint now`) and returns a RESP-encoded string.
6. The response is appended to `Client::write_buf` and non-blocking `send()` flushes it.

**Key types:**
- `RedisObject` — `std::variant<RedisString, RedisHash, RedisList, RedisSet, shared_ptr<SkipList>>` plus optional `expire_at`. All values are heap-allocated behind `shared_ptr<RedisObject>`.
- `Dict` — `unordered_map<string, shared_ptr<RedisObject>>`, the global key space. Single-threaded, no locks.
- `RespCodec` — stateless serializer (static methods); the parser side is per-connection (one `RespCodec` per `Client`), buffers partial data until a complete RESP array arrives.

**Command registration pattern:** Each command group has a `Register*Commands(CommandRouter&)` function (declared in `command/router.h`, defined in `command/*_cmd.cpp`). The `RedisServer` constructor calls all registration functions. To add a new command: implement the handler and add it to the appropriate registration function.

**Expiry:** Lazy check on key access via `ExpireManager::LazyCheck()`. Periodic (random-sampling) deletion is stubbed out for stage 5.

## Implementation Stages

| Stage | Status | What |
|-------|--------|------|
| 1 | Done | Skeleton: event loop, RESP codec, PING/PONG |
| 2 | Done | String + Key commands (SET/GET/DEL/EXISTS/EXPIRE/TTL/PTTL/TYPE) |
| 3 | Pending | Hash + List commands |
| 4 | Pending | Set + ZSet commands (SkipList already implemented) |
| 5 | Pending | Periodic expiry |
| 6 | Pending | RDB + AOF persistence |
| 7 | Pending | Benchmark + README |

## Constraints

- C++20 (but GCC 8.1 on MinGW lacks `unordered_map::contains` and heterogenous lookup — use `find(k) != end()` and `std::string(key)` for `string_view` keys).
- No external dependencies beyond the C++ standard library and OS sockets.
- Commands use RESP protocol — `redis-cli` can connect directly.
