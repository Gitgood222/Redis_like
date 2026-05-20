# 开发文档 — redis_like

## 概述

`redis_like` 是一个用 C++20 从零实现的类 Redis 内存数据库，兼容 RESP 协议，可直接使用 `redis-cli` 连接操作。项目目标是在 2-3 周内完成，作为 C++ 系统设计能力的面试展示。

## 环境与构建

**编译要求：** CMake 3.20+，支持 C++20 的编译器（GCC 8+/Clang 10+/MSVC 2022+）

```bash
# 配置
cd build && cmake .. -G "MinGW Makefiles"   # Windows (MinGW)
cd build && cmake ..                         # Linux

# 构建
cmake --build .

# 运行服务
./redis_like.exe [端口号]   # 默认 6379

# 运行测试
./tests/test_resp.exe           # RESP 协议测试 (14项)
./tests/test_string_cmd.exe     # String/Key 命令测试 (23项)
ctest                           # 一次性运行全部
```

使用 `redis-cli` 连接验证：
```bash
redis-cli -p 6379 PING          # → PONG
redis-cli -p 6379 SET k v EX 60 # → OK
redis-cli -p 6379 GET k         # → "v"
```

## 架构总览

```
main.cpp
  └─ RedisServer (server.h/cpp)
       ├─ EventLoop (event_loop.h/cpp)   ← select() 多路复用
       ├─ RespCodec (resp_codec.h/cpp)   ← RESP 解析/序列化
       ├─ CommandRouter (command/)       ← 命令路由表
       │    ├─ RegisterServerCommands()  → PING/COMMAND/QUIT
       │    ├─ RegisterStringCommands()  → SET/GET
       │    └─ RegisterKeyCommands()     → DEL/EXISTS/EXPIRE/TTL/TYPE
       ├─ Dict (dict.h)                  ← 全局键空间 (unordered_map)
       ├─ ExpireManager (expire.h/cpp)   ← 惰性删除 + 定期删除(待)
       └─ ds/                            ← 数据结构
            ├─ skiplist.h/cpp            → 跳表 (ZSet 核心)
            ├─ list.h/cpp                → 双向链表
            └─ intset.h                  → 整数集合
```

### 请求处理链路

```
1. EventLoop::RunOnce()               select() 轮询 fd
2. OnClientEvent(fd, READABLE)        接收数据
3. RespCodec::Feed(buf, n)            解析为 RespCommand{name, args}
4. CommandRouter::Execute(ctx)        O(1) 查表，分发 handler
5. CmdHandler(ctx)                    执行业务逻辑
6. RespCodec::BulkString/Integer/...  序列化响应
7. send(fd, resp)                     非阻塞写回客户端
```

### 核心数据模型

```cpp
// 统一对象类型
struct RedisObject {
    ObjType  type;       // String / Hash / List / Set / ZSet
    ValueVariant value;  // std::variant<5种类型>
    optional<TimePoint> expire_at;  // 过期时间
};

using ValueVariant = variant<
    RedisString,        // std::string
    RedisHash,          // unordered_map<string, string>
    RedisList,          // deque<string>
    RedisSet,           // unordered_set<string>
    shared_ptr<SkipList> // 跳表
>;
```

所有对象通过 `shared_ptr<RedisObject>` 管理，`Dict` 是 `unordered_map<string, shared_ptr<RedisObject>>`。

### 命令上下文

```cpp
struct CmdContext {
    Dict&          db;       // 键空间引用
    ExpireManager& expire;   // 过期管理引用
    RespCommand    cmd;      // 解析后的命令
    TimePoint      now;      // 当前时间（惰性过期检查用）
};
```

## 开发指南

### 新增一条命令

1. 在对应 `src/command/*_cmd.cpp` 中添加静态 handler 函数：

```cpp
static std::string MyCmd(CmdContext& ctx) {
    if (ctx.cmd.args.empty())
        return RespCodec::Error("ERR wrong number of arguments");
    // ... 业务逻辑 ...
    return RespCodec::Ok();
}
```

2. 在对应的 `Register*Commands()` 函数中注册：

```cpp
r.Register("MYCMD", MyCmd);
```

3. 在 `tests/` 中添加测试用例。

4. 重新构建并运行测试。

### 新增一种数据类型

1. 在 `common.h` 的 `ObjType` 枚举中新增类型。
2. 在 `object.h` 的 `ValueVariant` 中加入新类型。
3. 在 `RedisObject` 中添加工厂方法 `CreateXxx()`。
4. 在 `ds/` 中创建对应的数据结构头文件。
5. 实现 `RegisterXxxCommands()` 并注册命令。
6. 在 `RedisServer` 构造函数中调用新的注册函数。

## 测试

测试无外部依赖，使用自制的最小测试框架（宏 `TEST` + `ASSERT_EQ` + `ASSERT_TRUE`）。

每个测试可执行文件在 `tests/CMakeLists.txt` 中声明其依赖的源文件，直接编译链接。

```cpp
TEST(set_get_basic) {
    TestFixture fx;                          // 创建 Dict + Expire + Router
    ASSERT_EQ(fx.Exec("SET", {"k", "v"}), "+OK\r\n");
    ASSERT_EQ(fx.Exec("GET", {"k"}), "$1\r\nv\r\n");
}
```

`TestFixture` 在测试文件顶部定义，封装了 `Dict`, `ExpireManager`, `CommandRouter` 的创建和命令注册。

## 编码约定

- **语言标准**: C++20，不使用外部库
- **内存管理**: 所有对象通过 `shared_ptr` 管理，RAII
- **命令大小写**: 命令名统一转为大写做大小写不敏感匹配
- **单线程模型**: 与 Redis 一致，无锁、无竞态
- **错误响应**: 使用 `RespCodec::Error("ERR ...")`
- **过期检查**: 访问 key 前必须调用 `CheckExpire()` 做惰性删除

## 实现阶段

| 阶段 | 状态 | 内容 |
|------|------|------|
| 1 | ✅ 完成 | 项目骨架、事件循环、RESP 协议、PING/PONG |
| 2 | ✅ 完成 | String + Key 命令（SET/GET/DEL/EXISTS/EXPIRE/TTL/TYPE） |
| 3 | ✅ 完成 | Hash + List 命令 |
| 4 | ✅ 完成 | Set + ZSet 命令（跳表） |
| 5 | ✅ 完成 | 定期过期删除 |
| 6 | ✅ 完成 | RDB 快照 + AOF 日志持久化 |
| 7 | ✅ 完成 | 压测对比 + README |
