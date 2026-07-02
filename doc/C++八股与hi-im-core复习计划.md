# C++ 八股 · hi-im-core 复习计划（8 天 × 10 小时）

> **版本**：v1.0 · 2026-07-01  
> **目标**：投 IM 岗 + 用 **hi-im-core 故事线** 验证 C++ 八股，面试能「定义 → 项目里哪一行 → 为什么这样设计」  
> **配套**：[hi-im-core-核心代码精读.md](hi-im-core-核心代码精读.md)（逐文件、逐段映射）

---

## 1. 你的计划好不好？

| 你的做法 | 评价 | 建议 |
|----------|------|------|
| 8 天 × 10h 系统过视频 | ✅ 适合 **补齐 hi-im-core 没覆盖的八股** | 视频 **≤40%** 时间；**≥60%** 读代码 + 口述 + 手写 |
| 精读 hi-im-core 核心代码 | ✅ **ROI 最高** | 按本文 **Day 1～5 阅读顺序**，不要从 main 随机看 |
| 八股与项目绑定 | ✅ IM/C++ 岗必杀 | 每学一题先问：**hi-im-core 里有没有？哪一行？** |
| 同时投 IM 岗 | ✅ 正确 | **每天固定 1h 投递**，八股不能拖投递 |

**结论：** 计划方向对。唯一风险是 **10h 全看视频或全背题** → 改成 **「3h 视频/题 + 5h 代码 + 1h 口述录音 + 1h 投递」**。

---

## 2. 时间分配模板（每天 10h）

| 时段 | 时长 | 内容 |
|------|------|------|
| 上午 | 3h | 视频/书：当日八股主题（见 Day 表） |
| 下午 | 4h | hi-im-core **精读 + 在 IDE 里跳转**（见精读文档） |
| 傍晚 | 1h | **口述**：「这题八股 + 我项目里…」（录音 5 分钟） |
| 晚上 | 1h | **手写/白板**：虚表、内存序、epoll 流程、智能指针 |
| 固定 | 1h | 投 IM / GoCpp 简历 + 回消息 |

---

## 3. hi-im-core 覆盖了哪些八股？（总表）

图例：**★** 项目内有直接代码 · **△** 设计决策可讲、代码未深用 · **✗** 项目未覆盖，需单独背

### 3.1 语言与内存

| 八股主题 | hi-im-core | 核心位置 | 面试怎么说 |
|----------|------------|----------|------------|
| RAII | ★ | `PipeWakeup` 析构 close fd；`Reactor::Join` | 资源与生命周期绑定，Stop/Join Graceful shutdown |
| 智能指针 | △ | `unique_ptr<SpscQueue>` in `context_impl.cpp` | 队列所有权在 HubContext；**未用 shared_ptr** → 可说为何 SPSC 不需要共享所有权 |
| 移动语义 / std::move | ★ | `SpscQueue::Push`；`RegisterHandler(std::move(handler))` | 队列满时避免拷贝大 payload |
| 左值/右值引用 | ★ | 同上 | Push 接 T value 再 move 进 slot |
| 拷贝/移动构造 delete | ★ | `PipeWakeup(const PipeWakeup&) = delete` | fd 不可拷贝，防 double-close |
| 模板 | ★ | `SpscQueue<T>`、`PushWithBackoff<T>` | 编译期多类型队列 |
| std::optional | ★ | `SpscQueue::Pop`、`TryPopFrame` | 空队列 vs 有值，不用裸指针 |
| std::span | ★ | `SendBytes`、`EncodeFrame`、`Append` | C++20 非 owning 视图，零拷贝传 payload |
| std::function | ★ | `MessageHandler`、bridge lambda | 业务 handler 注册 |
| lambda 捕获 | ★ | `bridge.cpp`、`distributor.cpp` | `[this]`、`[&]` 区别要能讲 |
| enum class | ★ | `Plane`、`StatusCode`、`SysCmd` | 强类型，避免魔数 |
| `#pragma pack` / 内存对齐 | ★ | `WireHeader`；`alignas(64)` on queue atomics | 网络字节序 + 伪共享 |
| static_assert | ★ | `sizeof(WireHeader)==20` | 协议兼容硬约束 |
| 字节序 | ★ | `HostToBe32` / `BeToHost32` | 大端 wire 与主机序转换 |
| 虚函数 / 多态 | ✗ | **无继承多态** | 需单独背；可说 hub 用 **组合+std::function** 代替虚表 |
| 虚表 / 菱形继承 | ✗ | 无 | 八股必考，**Day 6 专背** |
| new/delete 重载 | ✗ | 几乎全栈对象 | 提一句用 vector/unique_ptr 避免裸 new |

### 3.2 并发与内存序

| 八股主题 | hi-im-core | 核心位置 | 面试怎么说 |
|----------|------------|----------|------------|
| 线程模型 | ★ | Listener / Reactor×N / Worker×N / Distributor | **one loop per thread** |
| std::thread | ★ | `Reactor::Start` → `thread_ = std::thread([this]{Run();})` | 线程入口与 Join 清理 |
| atomic | ★ | `running_`、`next_sid_`、SPSC head/tail | 停止标志与无锁队列 |
| memory_order | ★ | `queue.hpp` acquire/release/relaxed | **Day 2 必啃**；能画 happens-before |
| mutex / shared_mutex | ★ | `router.cpp` 读写锁；`handler_mu_` | 读多写少 SUB 表用 shared_lock |
| 死锁 | △ | 锁顺序固定（router 单 mu） | 可说 SPSC **无锁**避免 reactor-worker 死锁 |
| 条件变量 | ✗ | 用 **eventfd 唤醒** 代替 | Day 4 对比 cv vs eventfd |
| 线程池 | △ | 固定 Worker 线程，非动态池 | 可说 IM 热路径要 **绑核/固定线程** |
| false sharing | ★ | `alignas(64) atomic head_/tail_` | 双核写相邻 cache line 的性能 |

### 3.3 网络与 IO

| 八股主题 | hi-im-core | 核心位置 | 面试怎么说 |
|----------|------------|----------|------------|
| TCP 三次握手/四次挥手 | △ | Listener accept | 结合 **TIME_WAIT、SO_REUSEADDR**（listener 可讲） |
| 阻塞/非阻塞 | ★ | `SetNonBlocking`；`EAGAIN` 分支 | reactor 必须非阻塞 |
| epoll ET/LT | ★ | `reactor.cpp` LT（默认 level） | **Day 3 专讲**；为何 LT + 循环 recv 到 EAGAIN |
| epoll_wait 超时 | ★ | timeout=0  busy poll 倾向 | 换 latency vs CPU |
| kqueue | ★ | macOS 分支 | 跨平台条件编译 |
| eventfd / pipe 唤醒 | ★ | `pipe_wakeup.hpp` | 跨线程通知 reactor 有新连接/待发数据 |
| send/recv 短写短读 | ★ | `HandleWritable` 循环 send；`outbuf` | 应用层拼包发送 |
| TCP_NODELAY | ★ | `socket_tuning.hpp` | 小包 IM 降 Nagle 延迟 |
| SO_RCVBUF/SNDBUF | ★ | 512KB | 高吞吐缓冲 |
| Reactor 模型 | ★ | 全链路 | 对比 Proactor（未用 io_uring） |
| io_uring / sendfile | ✗ | 未用 | **八股了解即可** |
| 粘包/拆包 | ★ | `frame_buffer.hpp` | length-prefix 20B 头 + payload |
| 字节序 / 网络编程 | ★ | `header.hpp` | 与 Java Netty 同类问题 |

### 3.4 数据结构与 STL

| 八股主题 | hi-im-core | 核心位置 |
|----------|------------|----------|
| unordered_map | ★ | `sessions_`、`sub_table_`、`nid_map_` |
| vector | ★ | `outbuf`、`buf_`、订阅者列表 |
| map vs unordered_map | ★ | Router 用 hash 表 O(1) 查 nid |
| 迭代器失效 | ★ | `CloseSession` erase 时先 ++it |
| std::remove_if + erase | ★ | `router.cpp` Unsubscribe |

### 3.5 工程与其它

| 八股主题 | hi-im-core | 说明 |
|----------|------------|------|
| CMake / 单测 | ★ | ctest、integration test |
| Docker 压测 | ★ | bench 153942 recv/s — **必背数字** |
| 设计模式 | △ | Reactor、Bridge（双平面）、无单例滥用 |

---

## 4. hi-im-core **未覆盖**、但必须背的八股（按优先级）

> IM / C++ 后端岗 interview 高频，**别指望从项目里「看到」**，Day 6～8 专背 + 口述「hub 为什么没用/将来怎么用」。

### P0（几乎必问）

| 主题 | 为何项目里没有 | 面试兜底话术 |
|------|----------------|--------------|
| **虚函数、虚表、纯虚、虚析构** | hub 用 function 回调 | 「热路径避免虚调用开销；控制面 SDK 可以用接口类」 |
| **shared_ptr 循环引用 / weak_ptr** | 无 shared 图 | 「连接生命周期由 Session map 拥有，类似 unique 语义」 |
| **unique_ptr / make_unique** | 用了 unique 容器 | 结合 `conn_queues_` 讲所有权 |
| **const  correctness** | 部分 const 方法 | 补读 `FindSubscribers` const |
| **STL 容器底层** | vector/map 实现 | 「vector 连续内存适合 outbuf；unordered_map 桶查 nid」 |
| **进程 vs 线程** | 单进程多线程 | 「双平面在同一进程两端口；Phase2 可 shard 多进程」 |

### P1（高频）

| 主题 | 建议 |
|------|------|
| 内存分区（栈/堆/静态） | 画一帧 `InboundMessage` 在栈/堆 |
| 深拷贝 vs 浅拷贝 | `payload.assign` 是深拷贝 |
| 移动后对象状态 | move 进 queue 后勿再读 |
| 智能指针 deleter / 定制 | 了解即可 |
| 右值引用 std::forward | 模板 Push 可提 |
| 锁粒度 / 读写锁 | 直接讲 router |
| 惊群 / EPOLLEXCLUSIVE | epoll 多 reactor 可延伸 |
| TIME_WAIT / CLOSE_WAIT | 结合长连接断开 |

### P2（有时间再看）

| 主题 |
|------|
| C++11/14/17/20 特性清单 |
| 模板特化、SFINAE、concept |
| 异常 vs 错误码（hub 用 Status） |
| placement new、对象池 |
| tcmalloc/jemalloc、NUMA |
| io_uring、DPDK |

---

## 5. 核心代码阅读顺序（5 天主线）

> 详注见 [hi-im-core-核心代码精读.md](hi-im-core-核心代码精读.md)

| 顺序 | 文件 | 时长 | 掌握什么 |
|------|------|------|----------|
| ① | `include/hiim/wire/header.hpp` | 2h | 协议、字节序、pack、span |
| ② | `src/wire/frame_buffer.hpp` | 1h | 粘包拆包 |
| ③ | `src/hub/queue.hpp` + `queue_push.hpp` | 3h | **SPSC + memory_order**（Day2 核心） |
| ④ | `src/hub/pipe_wakeup.hpp` | 1h | eventfd、RAII、delete 拷贝 |
| ⑤ | `src/hub/reactor.cpp` | 4h | **epoll 主循环**（Day3 核心） |
| ⑥ | `src/hub/worker.cpp` + `distributor.cpp` | 2h | 线程分工、唤醒 |
| ⑦ | `src/hub/router.cpp` + `context_impl.cpp` | 3h | 锁、Publish/AsyncSend |
| ⑧ | `src/hub/bridge.cpp` | 1h | 双平面故事 |
| ⑨ | `src/hub/listener.cpp` | 1h | accept、非阻塞 |
| ⑩ | `test/*` + `cmd/hi-im-bench` | 2h | 如何证明 15 万 recv/s |

**线程全景（必画）：**

```text
Listener ──► ConnQueue ──eventfd──► Reactor ──RecvQueue──► Worker ──handler──► DistQueue ──► Distributor ──SendQueue ──► Reactor ──► TCP
```

---

## 6. 八天日程表

| Day | 八股主题（视频/书） | hi-im-core 精读 | 口述题（录音） |
|-----|---------------------|-------------------|----------------|
| **1** | C++ 类型基础、const、引用、RAII | ① header ② frame_buffer | 20B 协议怎么解析？粘包怎么办？ |
| **2** | 智能指针、移动语义、右值 | ③ queue ④ pipe_wakeup | SPSC 为什么 acquire/release？ |
| **3** | 线程、mutex、atomic、内存序 | ③ 再啃 + ⑤ reactor 前半 | epoll LT 下 recv 怎么写？EAGAIN 呢？ |
| **4** | 网络 TCP、IO 多路复用 | ⑤ reactor 后半 ⑥ worker/distributor | Reactor 和 Proactor 区别？为何 eventfd？ |
| **5** | epoll 深度、TCP 调优 | ⑦ router/context ⑧ bridge ⑨ listener | publish 和 async_send 路由区别？ |
| **6** | **虚表、继承、多态、STL 底层** | ⑩ test/bench；回顾全流程 | 为什么 hub 不用虚函数？ |
| **7** | 未覆盖 P0 扫盲 + 手写代码 | 全流程 Debug 跟一条 publish | 从 bench 发一条消息走哪些队列？ |
| **8** | 模拟面试：八股快问 + 项目 30min | 弱项补漏 | **完整 30 分钟项目串讲** |

---

## 7. 八股 ↔ 项目 标准答法模板

```text
面试官：讲讲 memory_order？

你：
1. 定义：C++11 原子操作的可见性约束…
2. 项目：hi-im-core 的 SpscQueue，tail 用 relaxed 读、acquire 判满，
   release 发布 tail；因为只有一个生产者一个消费者…
3. 文件：src/hub/queue.hpp 第 31-38 行
4. 若问 false sharing：head/tail alignas(64)…
```

每题尽量 **30 秒定义 + 60 秒项目 + 30 秒 trade-off**。

---

## 8. 与投递并行

| 事项 | 频率 |
|------|------|
| Tier 1 IM 专投 | 每天 2～3 个 JD |
| 期望薪资 | 面聊锚 **35K+**，不因 8 天复习暂停 |
| hi-file-transfer | 8 天内 **不启动**（避免分散） |

---

## 9. 后续文档（Phase 2）

| 文档 | 状态 |
|------|------|
| [hi-im-core-核心代码精读.md](hi-im-core-核心代码精读.md) | ✅ 逐段注释 + 八股标签 |
| `doc/hi-im-core-八股速查卡.md` | 可选：一页纸考前翻 |
| 源码内联注释版 `doc/annotated/` | 按需：从精读文档抽离 |

---

## 10. 相关文档

| 文档 | 用途 |
|------|------|
| [技术设计文档.md](技术设计文档.md) | 架构与 API 语义 |
| [简历-双版本投递指南.md](简历-双版本投递指南.md) | 30 分钟口述提纲 |
| [M1-实施清单.md](M1-实施清单.md) | 模块清单 |

---

*维护：完成 8 天复习后在本文件底部记录弱项清单。*
