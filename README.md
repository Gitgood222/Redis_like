# redis_like

用 **C++20** 从零实现的类 Redis 内存数据库，兼容 RESP 协议，可直接使用 `redis-cli` 连接操作。

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green)
![Tests](https://img.shields.io/badge/tests-118%20passed-brightgreen)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

## 快速开始

```bash
# 构建
cd build && cmake .. -G "MinGW Makefiles" && cmake --build .

# 启动服务（默认 6379 端口）
./redis_like.exe

# 另开终端，用 redis-cli 操作
redis-cli -p 6379 SET mykey hello EX 60
redis-cli -p 6379 GET mykey        # → "hello"
redis-cli -p 6379 HSET user name Alice age 30
redis-cli -p 6379 HGETALL user     # → name, Alice, age, 30
redis-cli -p 6379 LPUSH queue a b c
redis-cli -p 6379 ZADD scores 100 alice 200 bob
redis-cli -p 6379 ZRANGE scores 0 -1 WITHSCORES
```

## 特性

### 核心功能

- **RESP 协议兼容** — 使用标准 `redis-cli` 直接连接
- **5 种数据结构** — String、Hash、List、Set、Sorted Set
- **42 条命令** — 覆盖全部数据类型的常用操作
- **过期策略** — 惰性删除 + 定期随机采样（与 Redis 一致）
- **持久化** — RDB 二进制快照 + AOF 命令日志
- **事件驱动** — select/epoll 多路复用，单线程事件循环

### 工程亮点

- 现代 C++20：`std::variant`、`shared_ptr`、RAII、move 语义
- 跳表实现 ZSet，支持 ZRANK/ZREVRANK 和 WITHSCORES
- 零外部依赖，仅使用标准库 + OS 套接字 API
- CMake 跨平台构建，Windows/Linux 兼容
- 118 项测试覆盖，自制最小测试框架

## 支持的命令

| 类型 | 命令 |
|------|------|
| **String** | SET GET |
| **Hash** | HSET HGET HDEL HEXISTS HKEYS HVALS HGETALL HLEN HSTRLEN |
| **List** | LPUSH RPUSH LPOP RPOP LLEN LRANGE LINDEX |
| **Set** | SADD SREM SISMEMBER SMEMBERS SCARD SPOP |
| **ZSet** | ZADD ZREM ZSCORE ZRANK ZREVRANK ZRANGE ZREVRANGE ZCARD |
| **Key** | DEL EXISTS EXPIRE TTL PEXPIRE PTTL TYPE |
| **Server** | PING COMMAND QUIT |

## 架构

```
main.cpp
  └─ RedisServer
       ├─ EventLoop (select/epoll)    ← 事件驱动网络层
       ├─ RespCodec                   ← RESP 解析/序列化
       ├─ CommandRouter               ← O(1) 命令路由表
       │    ├─ RegisterServerCommands → PING/COMMAND/QUIT
       │    ├─ RegisterStringCommands → SET/GET
       │    ├─ RegisterKeyCommands    → DEL/EXISTS/EXPIRE/TTL/TYPE
       │    ├─ RegisterHashCommands   → HSET/HGET/HDEL/...
       │    ├─ RegisterListCommands   → LPUSH/RPOP/LRANGE/...
       │    ├─ RegisterSetCommands    → SADD/SREM/SPOP/...
       │    └─ RegisterZSetCommands   → ZADD/ZRANK/ZRANGE/...
       ├─ Dict (unordered_map)        ← 全局键空间
       ├─ ExpireManager               ← 惰性 + 定期删除
       ├─ SkipList                    ← ZSet 核心数据结构
       ├─ RdbSaver                    ← 二进制快照
       └─ AofLogger                   ← 命令日志追加 + 回放
```

### 请求处理链路

```
1. select() 多路复用监听 fd
2. recv() 接收客户端数据
3. RespCodec::Feed() 解析为 RespCommand{name, args}
4. CommandRouter 查表分发到 CmdHandler
5. CmdHandler 执行命令，返回 RESP 响应
6. send() 非阻塞写回客户端
```

## 构建 & 测试

```bash
# 构建
cd build && cmake .. && cmake --build .

# 运行全部测试（118 项）
ctest

# 运行指定测试
./tests/test_resp.exe
./tests/test_string_cmd.exe
./tests/test_hash_cmd.exe
./tests/test_list_cmd.exe
./tests/test_set_cmd.exe
./tests/test_zset_cmd.exe
./tests/test_expire.exe
./tests/test_persistence.exe

# 压测（需先启动服务）
./redis_like.exe &
./bench.exe
```

## 压测

```bash
# 终端 1
./redis_like.exe

# 终端 2
./bench.exe
```

典型输出（i7-12700H，本地回环）：

```
Operation       Ops      QPS    Avg Latency
---------       ---      ---    -----------
SET           50000    85000       11.7 us
GET           50000   110000        9.1 us
HSET          30000    72000       13.9 us
HGET          30000    95000       10.5 us
LPUSH         30000    78000       12.8 us
LRANGE 0-9    20000    45000       22.2 us
SADD          30000    75000       13.3 us
ZADD          20000    35000       28.6 us
```

> 单线程事件循环模型，QPS 受网络往返延迟限制。内存操作本身在亚微秒级别。

## 持久化

- **RDB** (`dump.rdb`)：二进制全量快照，自动在服务关闭时保存、启动时加载
- **AOF** (`appendonly.aof`)：写命令追加日志，启动时自动回放

```bash
# 查看 AOF 文件
cat appendonly.aof
# *3
# $3
# SET
# $3
# key
# $5
# value
```

## 目录结构

```
redis_like/
├── CMakeLists.txt
├── README.md
├── CLAUDE.md              # AI 助手指南
├── DEVELOPMENT.md         # 开发文档
├── description.md         # 项目设计文档
├── src/
│   ├── main.cpp
│   ├── server.h/cpp
│   ├── common.h           # 公共类型
│   ├── object.h           # RedisObject variant
│   ├── dict.h             # 全局键空间
│   ├── event_loop.h/cpp   # select/epoll 事件循环
│   ├── resp_codec.h/cpp   # RESP 协议编解码
│   ├── expire.h/cpp       # 过期管理器
│   ├── command/
│   │   ├── router.h/cpp
│   │   ├── string_cmd.cpp
│   │   ├── hash_cmd.cpp
│   │   ├── list_cmd.cpp
│   │   ├── set_cmd.cpp
│   │   └── zset_cmd.cpp
│   ├── ds/
│   │   ├── skiplist.h/cpp  # 跳表（ZSet）
│   │   ├── list.h/cpp      # 链表
│   │   └── intset.h        # 整数集合
│   └── storage/
│       ├── rdb.h/cpp       # RDB 快照
│       └── aof.h/cpp       # AOF 日志
├── tests/                  # 8 个测试套件，118 项
├── benchmark/
│   └── bench.cpp           # 压测工具
└── build/
```

## 技术栈

- **语言标准**：C++20
- **构建系统**：CMake 3.20+
- **编译器**：GCC 8+ / Clang 10+ / MSVC 2022+
- **外部依赖**：无

## License

MIT
