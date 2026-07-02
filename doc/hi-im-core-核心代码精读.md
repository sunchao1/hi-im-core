# hi-im-core 核心代码精读（八股映射版）

> **版本**：v1.0 · 2026-07-01  
> **用法**：配合 [C++八股与hi-im-core复习计划.md](C++八股与hi-im-core复习计划.md) Day 1～5 按文件阅读  
> **说明**：下列为 **学习用逐段注释**（非源码内联）；行号以当前 `feature/base` 为准，改代码后需重新核对

**八股标签**：`[RAII]` `[move]` `[atomic]` `[epoll]` `[mutex]` `[span]` `[pack]` …

---

## 0. 先背这条数据路径

```text
TCP bytes → FrameBuffer::TryPopFrame → Reactor::HandleSystem/EnqueueInbound
  → SpscQueue<InboundMessage> → Worker::handler → Publish/AsyncSend
  → SpscQueue<OutboundFrame> DistQueue → Distributor → SendQueue
  → Reactor::SendBytes → TCP
```

Bridge 路径：FORWARD 默认 handler → `peer->Publish`；BACKEND → 读 IM nid → `peer->AsyncSend`。

---

## 1. `include/hiim/wire/header.hpp`

**当日八股**：字节序、`#pragma pack`、`static_assert`、`memcpy` 与对齐、`std::span`

| 行号 | 代码要点 | 八股 / 面试 |
|------|----------|-------------|
| 32-40 | `#pragma pack(push,1)` + `WireHeader` | 结构体 1 字节对齐，保证 wire **20 字节** 与必嗨兼容 |
| 42 | `static_assert(sizeof==20)` | 编译期断言；改字段立刻失败 |
| 44-52 | `HostToBe32` / `__builtin_bswap32` | 大端网络序；小端主机需 swap |
| 56-58 | `ValidateChecksum` | 固定 magic，防脏数据 |
| 71-76 | `EncodeHeader` + `memcpy` | POD 整块拷贝；注意 **未处理 endian 字段已是 BE** |
| 78-87 | `DecodeHeader` | 先长度够再拷；checksum 失败 return false |
| 89-98 | `EncodeFrame` 返回 `vector` | 堆分配一帧；热路径可优化为复用 buffer（trade-off 题） |
| 101-103 | 成员指针 `WireHeader::* field` | C++ 成员指针较少见；可读字段 |

**口述题**：为什么不用 `struct __attribute__((packed))` 而用 `#pragma pack`？—— 效果类似，MSVC/GCC 可移植写法。

**项目未覆盖**：位域 bitfield、可变长编码 Protobuf（payload 仍是 raw bytes）。

---

## 2. `src/wire/frame_buffer.hpp`

**当日八股**：粘包拆包、状态机、`optional`、迭代器

| 行号 | 代码要点 | 八股 / 面试 |
|------|----------|-------------|
| 28-31 | `FrameView` + `span<const uint8_t> payload` | **非 owning 视图**；帧还在 `buf_` 里 |
| 35-37 | `Append` insert 尾部 | TCP 流累积 |
| 43-46 | 不足 20B return nullopt | **半包** 等待更多 recv |
| 48-52 | `DecodeHeader` 失败 clear | 协议错误 **整缓冲丢弃**（可讨论是否过于激进） |
| 54-58 | `payload_len` + `frame_len` | length-prefix 定界 |
| 60-62 | `TryPopFrame` erase 已消费 | **拆包**；vector erase 前端 O(n) — 可讲优化为 ring buffer |

**口述题**：Nagle + 小包 vs 长度前缀？—— hub 用 TCP_NODELAY + 20B 头。

---

## 3. `src/hub/queue.hpp` ⭐ Day 2 核心

**当日八股**：SPSC 无锁队列、memory_order、false sharing、模板、`optional`

```cpp
// 伪注释学习版 — 对照源码 queue.hpp

template <typename T>
class SpscQueue {
  // [模板] 编译期类型，InboundMessage / OutboundFrame / NewConnection 复用

  bool Push(T value) {                    // [右值] 按值接，便于 move
    tail = tail_.load(relaxed);           // 生产者只写 tail，读 tail 用 relaxed 够吗？—— 单生产者内顺序
    next = (tail+1) % cap;
    if (next == head_.load(acquire))      // [acquire] 读消费者进度，见已提交 slot
      return false;                       // 满
    slots_[tail] = move(value);           // [move] 大 vector payload 避免拷贝
    tail_.store(next, release);           // [release] 发布新 tail，消费者 acquire 可见
    return true;
  }

  optional<T> Pop() {
    head = head_.load(relaxed);
    if (head == tail_.load(acquire))      // 空
      return nullopt;
    value = move(slots_[head]);
    head_.store(..., release);
    return value;
  }

  alignas(64) atomic<size_t> head_;       // [false sharing] 与 tail 分 cache line
  alignas(64) atomic<size_t> tail_;
};
```

**必背**：为什么 SPSC 不需要 mutex？—— **一个线程只写 tail，一个只写 head**；happens-before 由 release/acquire 建立。

**对比八股**：MPSC、MPMC 不能用这个简单实现 → 需 mutex 或复杂无锁。

---

## 4. `src/hub/queue_push.hpp`

| 行号 | 要点 | 八股 |
|------|------|------|
| 10-19 | 队列满 spin + yield | 背压；`yield` 让出 CPU；极端满返回 false 上层打日志 |
| 15-16 | `i & 0x3F == 0` yield | 避免纯 busy-loop 占满核 |

**口述题**：为什么不用 blocking queue？—— reactor 线程不能阻塞在 mutex+cv 上等 worker（延迟）。

---

## 5. `src/hub/pipe_wakeup.hpp`

**当日八股**：RAII、Rule of Five、`delete` 拷贝、eventfd

| 行号 | 要点 | 八股 |
|------|------|------|
| 31-44 | 构造：Linux eventfd / macOS pipe | 跨平台 **条件编译** |
| 46-55 | 析构 close | **[RAII]** fd 生命周期 |
| 57-58 | 拷贝 delete | fd 不可共享所有权 |
| 63-79 | `Notify` write 1 | 唤醒 epoll_wait 侧 |
| 81-90 | `Drain` 读空 | 水平触发下 **必须 drain** 否则一直可读 |

**对比八股**：`condition_variable` vs eventfd —— eventfd 可 **fd 进 epoll**，统一事件源。

---

## 6. `src/hub/reactor.cpp` ⭐ Day 3 核心

**当日八股**：epoll、非阻塞 IO、EAGAIN、线程、map、lambda

### 6.1 生命周期

| 行号 | 要点 |
|------|------|
| 67-88 | `Start`：epoll_create1(EPOLL_CLOEXEC)；注册 wakeup fd；`std::thread([this]{Run()})` |
| 93-107 | `Join`：join 线程；close epfd |

### 6.2 epoll 与 interest

| 行号 | 要点 | 八股 |
|------|------|------|
| 114-127 | `UpdateInterest` EPOLLIN/EPOLLOUT | 写缓冲非空时关注 **可写** |
| 141-150 | 新连接 EPOLL_CTL_ADD | LT 默认 |
| 207-208 | Close EPOLL_CTL_DEL | 避免 epoll 脏事件 |

### 6.3 读路径

| 行号 | 要点 | 八股 |
|------|------|------|
| 319-340 | `while recv` 直到 EAGAIN | **[LT]** 必须读空；ET 则不同 |
| 324-332 | `TryPopFrame` 循环 | 一包 TCP 可能多帧 |
| 336-338 | n==0 或对端关闭 | 四次挥手 FIN → recv 0 |

### 6.4 写路径

| 行号 | 要点 | 八股 |
|------|------|------|
| 165-172 | `outbuf` 追加 + HandleWritable | **短写** 常见 |
| 181-194 | loop send 直到 EAGAIN | 非阻塞 send |

### 6.5 跨线程

| 行号 | 要点 |
|------|------|
| 130-151 | `DrainNewConnections` 从 SPSC 取 fd |
| 154-162 | `DrainSendQueue` |
| 296-302 | `EnqueueInbound` Push recv queue + WorkerWakeup |

### 6.6 主循环

| 行号 | 要点 | 八股 |
|------|------|------|
| 344 | `Running().load(acquire)` | 停止标志可见性 |
| 347 | `epoll_wait(..., 0)` | **零超时** = 高 CPU 换低延迟（M1 优化点） |
| 348-355 | 分发 EPOLLIN/OUT | 经典 Reactor |

**系统命令**（219-279）：AUTH/SUB/KPALIVE — 面试可简讲，八股关联 **状态机**。

---

## 7. `src/hub/worker.cpp`

| 行号 | 要点 | 八股 |
|------|------|------|
| 48 | worker 线程 | 与 reactor **1:N**（PickWorker by sid） |
| 72-90 | epoll 只监听 wakeup | worker **不直接 epoll 客户端 fd** |
| 92-99 | Pop recv queue → handler | **生产者 reactor，消费者 worker** |
| 94-96 | handler 0 默认 | bridge 注册 cmd=0 |

---

## 8. `src/hub/distributor.cpp`

| 行号 | 要点 |
|------|------|
| 73-83 | Publish 结果进 DistQueue，再 route 到 SendQueue |
| 105-107 | Pop DistQueue | 单线程 distributor，避免多 reactor 竞争写 map |

---

## 9. `src/hub/router.cpp`

**当日八股**：`shared_mutex`、读写锁、unordered_map、迭代器

| 行号 | 要点 | 八股 |
|------|------|------|
| 24-35 | Subscribe unique_lock | 写路径 |
| 70-76 | FindSubscribers shared_lock | **读多写少** |
| 45-50 | remove_if + erase | 经典 erase idiom |
| 88-90 | BindNid nid→(sid,reactor_idx) | async_send **O(1)** 查路由 |

**口述题**：为什么 Router 要锁而 SPSC 不要？—— **多 worker/reactor 可能并发读订阅表**（实际上写多在 reactor AUTH/SUB）；设计保守用锁。

---

## 10. `src/hub/context_impl.cpp`

| 行号 | 要点 | 八股 |
|------|------|------|
| 31-44 | `make_unique<SpscQueue>` | **[unique_ptr]** 队列所有权 |
| 49-51 | RegisterHandler + move | `std::function` 存储 |
| 89-90 | `next_sid_.fetch_add(relaxed)` | 仅要唯一 id，不需严格顺序 |
| 102-119 | Publish 循环订阅者 EncodeFrame | **Fan-out**；每订阅者一帧 |
| 122+ | AsyncSend FindNidRoute | **单播** |

---

## 11. `src/hub/bridge.cpp`

| 行号 | 要点 | 面试故事 |
|------|------|----------|
| 27-37 | 读 IM header offset 4 的 nid | bridge **不解析 PB**，只读固定偏移 |
| 40-48 | FORWARD → peer Publish | 上行进 BACKEND |
| 51-66 | BACKEND → peer AsyncSend | 下行到 gateway NID |
| 79-86 | lambda handler | `[](HubContext& ctx, const InboundMessage& msg)` |

**双平面**是 IM 岗 **30 秒架构图** 核心。

---

## 12. `src/hub/listener.cpp`（节选）

| 要点 | 八股 |
|------|------|
| `socket` + `bind` + `listen` + `accept` | TCP 服务端 |
| `SetNonBlocking` | accept 也非阻塞 |
| `PickReactor` atomic 轮询 | 新连接负载均衡到 reactor |
| Push ConnQueue + ReactorWakeup | 跨线程传递 fd |

---

## 13. `src/hub/socket_tuning.hpp`

| 要点 | 八股 |
|------|------|
| TCP_NODELAY | 禁用 Nagle，小包延迟 |
| 512KB SNDBUF/RCVBUF | 吞吐 vs 内存 |

---

## 14. `include/hiim/status.hpp`

| 要点 | 八股 |
|------|------|
| enum class StatusCode | 强类型错误码 |
| Error + move string | 不用异常控制流（网络服务常见） |

**对比八股**：异常 vs 错误码 —— hub 选 **Status**，热路径无 throw。

---

## 15. 测试与压测（证明你会讲数字）

| 文件 | 用途 |
|------|------|
| `test/wire_header_test.cpp` | 协议单测 |
| `test/frame_buffer_test.cpp` | 半包/多包 |
| `test/router_test.cpp` | SUB/nid 路由 |
| `test/integration/hub_proxy_test.cpp` | 端到端 |
| `cmd/hi-im-bench/main.cpp` | **153,942 recv/s** 怎么跑 |
| `bench/baseline-v1.json` | 可复现基线 |

**面试必答**：bench 条件 — 256B payload、10s、单 publisher、Docker compose、无持久化。

---

## 16. 精读 checklist（自测打勾）

- [ ] 能白板画 5 线程角色 + 4 种队列
- [ ] 能解释 SPSC 每一行 memory_order
- [ ] 能讲 epoll_wait timeout=0 的利弊
- [ ] 能讲 publish vs async_send
- [ ] 能讲 FORWARD/BACKEND/bridge
- [ ] 能背 M1 压测数字与命令
- [ ] 能答「hub 为什么几乎不用 shared_ptr/虚函数」

---

## 17. 项目内找不到的八股 → 去哪背

见 [C++八股与hi-im-core复习计划.md](C++八股与hi-im-core复习计划.md) **第 4 节 P0/P1**。

**口诀**：**语言 Day1-2，并发 Day2-4，网络 Day3-5，虚表/STL Day6，模拟 Day7-8。**

---

*Phase 2：若需 `doc/annotated/` 带注释源码副本，从本文 §3、§6 抽取即可。*
