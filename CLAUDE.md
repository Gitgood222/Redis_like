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

# Run all tests via ctest
cd build && ctest

# Or run individual test executables
./tests/test_resp.exe
./tests/test_string_cmd.exe
./tests/test_hash_cmd.exe
./tests/test_list_cmd.exe
./tests/test_set_cmd.exe
./tests/test_zset_cmd.exe
./tests/test_expire.exe
./tests/test_persistence.exe
```

Tests are standalone executables (no external test framework). Each `tests/test_*.cpp` compiles against the source files it depends on, declared in `tests/CMakeLists.txt`.

**Test framework:** Each test file defines a minimal framework in-place:
- `TEST(name)` — auto-registering test via static global. Uses `ASSERT_EQ(a, b)` and `ASSERT_TRUE(x)` macros that throw on failure.
- `TestFixture` — local struct holding `Dict`, `ExpireManager`, and `CommandRouter`. Its constructor calls the relevant `Register*Commands()` and provides an `Exec(cmd_name, args)` helper that builds a `CmdContext` and routes the command.

```cpp
TEST(set_get_basic) {
    TestFixture fx;
    ASSERT_EQ(fx.Exec("SET", {"k", "v"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$1\r\nv\r\n");
}
```

## Architecture

Single-threaded event-loop server modeled on Redis. The `EventLoop` uses epoll on Linux and `select()` on other platforms.

**Socket portability:** `socket_t` is `SOCKET` on Windows, `int` on Linux. `kInvalidSocket` and the `CLOSE_SOCKET` macro handle platform differences (`event_loop.h:7-22`).

**Request path:**
1. `main.cpp` → `RedisServer::Init()` creates a listening socket and registers it with the event loop, then loops on `Tick()`.
2. `EventLoop::RunOnce()` uses `select()` to multiplex between the listen fd and client fds.
3. On readable client fd, `OnClientEvent()` calls `recv()`, feeds bytes into `RespCodec::Feed()`, and dispatches each parsed `RespCommand`.
4. `CommandRouter::Execute()` looks up the command name (case-insensitive via `Normalize()`) in an `unordered_map<string, CmdHandler>` and invokes the handler.
5. The handler receives `CmdContext&` (references to `Dict`, `ExpireManager`, parsed `RespCommand`, `TimePoint now`) and returns a RESP-encoded string.
6. The response is appended to `Client::write_buf` and non-blocking `send()` flushes it.

**Key types:**
- `RedisObject` — `std::variant<RedisString, RedisHash, RedisList, RedisSet, shared_ptr<SkipList>>` plus optional `expire_at`. All values are heap-allocated behind `shared_ptr<RedisObject>`. Factory methods (`CreateString`, `CreateHash`, etc.) handle construction.
- `Dict` — `unordered_map<string, shared_ptr<RedisObject>>`, the global key space. Single-threaded, no locks.
- `RespCodec` — stateless serializer (static methods); the parser side is per-connection (one `RespCodec` per `Client`), buffers partial data until a complete RESP array arrives.
- `RespCommand` — parsed command with `.name` (uppercased) and `.args` (vector of string arguments, name already stripped).

**Command registration pattern:** Each command group has a `Register*Commands(CommandRouter&)` function (declared in `command/router.h`, defined in `command/*_cmd.cpp`). The `RedisServer` constructor calls all registration functions. To add a new command: implement a handler with signature `std::string(CmdContext&)` and add it to the appropriate registration function.

**Expiry:** Two mechanisms, matching Redis:
- Lazy deletion via `ExpireManager::LazyCheck()` — called before every key access.
- Periodic deletion via `ExpireManager::PeriodicCheck()` — random-sampling, triggered each `Tick()`.

**Persistence:**
- RDB (`storage/rdb.cpp`) — full binary snapshot saved on shutdown, loaded on startup.
- AOF (`storage/aof.cpp`) — write commands appended to `appendonly.aof`, replayed on startup. Only mutating commands are logged (reads are skipped).

## Code Conventions

- Command names are normalized to uppercase for case-insensitive matching (`CommandRouter::Normalize`).
- Error responses use `RespCodec::Error("ERR ...")`.
- Every key access must call `CheckExpire()` / `LazyCheck()` first for lazy deletion.
- Single-threaded model — no locks, no atomics needed.
- All heap objects managed via `shared_ptr`, RAII throughout.

## Constraints

- C++20 (but GCC 8.1 on MinGW lacks `unordered_map::contains` and heterogeneous lookup — use `find(k) != end()` and `std::string(key)` for `string_view` keys).
- No external dependencies beyond the C++ standard library and OS sockets.
- Commands use RESP protocol — `redis-cli` can connect directly.

## Implementation Stages

| Stage | Status | What |
|-------|--------|------|
| 1 | Done | Skeleton: event loop, RESP codec, PING/PONG |
| 2 | Done | String + Key commands (SET/GET/DEL/EXISTS/EXPIRE/TTL/PTTL/TYPE) |
| 3 | Done | Hash + List commands |
| 4 | Done | Set + ZSet commands (SkipList) |
| 5 | Done | Periodic expiry |
| 6 | Done | RDB + AOF persistence |
| 7 | Done | Benchmark + README |
