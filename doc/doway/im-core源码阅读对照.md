# hi-im-core 源码精读对照

> **用途**：统计核心代码体量，划分模块，给出精读路径与优先级。  
> **背景**：hi-im-core 相对必嗨 RTMQ 约 7000 行，体量缩小约一半，代码更紧凑，适合精读。

---

## 1. 总体规模

hi-im-core 是一个 **C++ Hub 消息总线** 项目，代码量不大，很适合精读。

| 范围 | 文件数 | 总行数 | 有效代码≈* |
|------|--------|--------|-----------|
| **核心库** (`include/` + `src/`) | 30 | **2,750** | ~1,700 |
| 入口 (`cmd/`) | 2 | 498 | ~417 |
| 测试 (`test/`) | 6 | 1,050 | ~824 |
| **C++ 合计** | 38 | **4,298** | ~3,200 |
| 文档 (`doc/`) | 12 | ~2,038 | — |

\*有效代码 = 去掉空行和注释后的估算，仅供参考。

**精读核心库大约 2,750 行（有效约 1,700 行）**，体量相当于一本中等篇幅技术书的一个章节，1–2 周可以精读一遍。

---

## 2. 模块拆分

| 模块 | 文件数 | 总行 | 空行 | 注释 | 有效≈ |
|------|--------|------|------|------|-------|
| wire 协议层 | 5 | 386 | 62 | 68 | 256 |
| hub 运行时 | 23 | 2,297 | 351 | 275 | 1,671 |
| 公共头 | 2 | 67 | 11 | 28 | 28 |
| 入口 cmd | 2 | 498 | 55 | 26 | 417 |
| 测试 test | 6 | 1,050 | 141 | 85 | 824 |
| **合计** | 38 | 4,298 | 620 | 482 | 3,196 |

### 2.1 wire 协议层（~386 行）

| 文件 | 行数 | 说明 |
|------|------|------|
| `include/hiim/wire/header.hpp` | 105 | 20 字节 WireHeader |
| `include/hiim/wire/auth.hpp` | 78 | AUTH 帧编解码 |
| `include/hiim/wire/sys_cmd.hpp` | 35 | 系统 cmd 枚举 |
| `include/hiim/im/header.hpp` | 95 | IM 52 字节头、PackPayload |
| `src/wire/frame_buffer.hpp` | 73 | TCP 拼帧缓冲 |

### 2.2 hub 运行时（~2,297 行）

| 文件 | 行数 | 说明 |
|------|------|------|
| `src/hub/reactor.cpp` | 396 | epoll Reactor，IO 核心 |
| `src/hub/listener.cpp` | 175 | 监听与 accept |
| `src/hub/context_impl.cpp` | 150 | HubContext、Publish/AsyncSend |
| `include/hiim/hub/context.hpp` | 137 | 上下文 API |
| `src/hub/distributor.cpp` | 128 | 出站分发 |
| `src/hub/health_server.cpp` | 124 | 健康检查 |
| `src/hub/route_log.hpp` | 112 | 路由日志 |
| `src/hub/router.cpp` | 110 | publish / async_send 路由表 |
| `src/hub/hub_server.cpp` | 106 | Hub 主服务、双平面启动 |
| `src/hub/queue.hpp` | 105 | SPSC / MPSC 队列 |
| `src/hub/worker.cpp` | 104 | Worker 消费 recvq |
| `src/hub/bridge.cpp` | 80 | FORWARD↔BACKEND bridge |
| 其余 | ~470 | reactor.hpp、worker.hpp、distributor.hpp 等 |

---

## 3. 核心文件 Top 10（优先读）

| 行数 | 文件 | 角色 |
|------|------|------|
| 396 | `src/hub/reactor.cpp` | epoll Reactor，IO 核心 |
| 175 | `src/hub/listener.cpp` | 监听与 accept |
| 150 | `src/hub/context_impl.cpp` | HubContext 实现 |
| 137 | `include/hiim/hub/context.hpp` | 上下文 API |
| 128 | `src/hub/distributor.cpp` | 出站分发 |
| 124 | `src/hub/health_server.cpp` | 健康检查 |
| 112 | `src/hub/route_log.hpp` | 路由日志 |
| 110 | `src/hub/router.cpp` | publish / async_send 路由 |
| 106 | `src/hub/hub_server.cpp` | Hub 主服务 |
| 105 | `src/hub/queue.hpp` | SPSC / MPSC 队列 |

这 10 个文件合计约 **1,543 行**，占核心库的 **56%**。

---

## 4. 建议精读路径

项目里已有阅读指南：[doc/theme/README.md](../theme/README.md)。建议顺序：

| 阶段 | 主题 | 行数约 | 读什么 |
|------|------|--------|--------|
| 1 | 协议层 | ~400 | `include/hiim/wire/` + `src/wire/frame_buffer.hpp` |
| 2 | Reactor 模型 | ~500 | `reactor.cpp/hpp`、`listener.cpp`、`pipe_wakeup.hpp` |
| 3 | 队列语义 | ~200 | `queue.hpp`、`queue_push.hpp` |
| 4 | 路由与双平面 | ~400 | `router.cpp`、`bridge.cpp`、`distributor.cpp` |
| 5 | 上下文与入口 | ~350 | `context.hpp`、`context_impl.cpp`、`hub_server.cpp` |
| 6 | 测试对照 | ~1,050 | 边读边跑 `ctest`，理解行为 |

配套文档约 **2,000 行**，尤其是 [doc/技术设计文档.md](../技术设计文档.md)（472 行）和 `doc/theme/` 下 6 篇专题，建议和源码对照读。

### 4.1 theme 专题阅读顺序（30 分钟过一遍）

| 顺序 | 文档 | 面试一句话 |
|------|------|------------|
| 1 | [01-epoll-Reactor模型.md](../theme/01-epoll-Reactor模型.md) | Hub 用 **Reactor × N + epoll**，连接 stick 到固定线程 |
| 2 | [02-SPSC与MPSC队列语义.md](../theme/02-SPSC与MPSC队列语义.md) | 四类队列按 **生产者数 × 消费者数** 选型 |
| 3 | [03-Bug2-MPSC误用SPSC.md](../theme/03-Bug2-MPSC误用SPSC.md) | DistQueue/RecvQueue 多写单读必须用 MPSC |
| 4 | [04-双平面FORWARD与BACKEND.md](../theme/04-双平面FORWARD与BACKEND.md) | bridge 负责 **上行 publish / 下行 async_send** |
| 5 | [05-async_send与publish路由.md](../theme/05-async_send与publish路由.md) | publish 按 cmd 广播；async_send 按 dest_nid 单播 |
| 6 | [06-群聊端到端背诵.md](../theme/06-群聊端到端背诵.md) | 跨 Gateway 群聊 **双段 fan-out** 全链路 |

---

## 5. 核心概念速查（读代码前先看）

| 概念 | 含义 |
|------|------|
| **NID** | Node ID，Proxy 进程实例唯一标识；AUTH 后绑定 TCP |
| **双平面** | FORWARD :28888（gateway）+ BACKEND :28889（msgsvr 等） |
| **publish** | 按 cmd 查 SUB 表，广播给订阅者 |
| **async_send** | 按 dest_nid 查 NID 表，单播到指定接入 |
| **bridge** | FORWARD 上行 → Publish；BACKEND 下行 → AsyncSend(FORWARD) |

### 5.1 线程模型（必嗨 → hi-im-core）

```text
Listener → connq(SPSC) → Reactor×N
Reactor → recvq(MPSC) → Worker×M → bridge / handler
Worker → distq(MPSC) → Distributor×1 → sendq(SPSC) → Reactor 写 TCP
```

| 队列 | 语义 |
|------|------|
| ConnQueue / SendQueue | **SPSC** |
| RecvQueue / DistQueue | **MPSC** |

---

## 6. 测试与验证（边读边跑）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| 测试 | 文件 | 验证什么 |
|------|------|----------|
| wire 头 | `test/wire_header_test.cpp` | 20 字节头编解码 |
| 拼帧 | `test/frame_buffer_test.cpp` | TCP 半包/粘包 |
| 路由 | `test/router_test.cpp` | SUB / NID 表 |
| bridge 上行 | `test/integration/hub_proxy_test.cpp` | FORWARD→BACKEND publish |
| bridge 下行 | `test/bridge_downlink_test.cpp` | 双 gateway 路由、dest_nid offset 24 |

---

## 7. 与业务文档的对照

| 读源码时 | 对照文档 |
|----------|----------|
| 理解 im-core 边界 | [技术设计文档.md](../技术设计文档.md) §1–3 |
| 线协议字段 | [协议规范-bus-wire-v1.md](../协议规范-bus-wire-v1.md) |
| 群聊在 im-core 里怎么走 | [理解核心1.md](./理解核心1.md)、[theme/06](../theme/06-群聊端到端背诵.md) |
| M1 验收与压测 | [M1-实施清单.md](../M1-实施清单.md) |

---

## 8. 一句话结论

**核心 C++ 约 2,750 行（30 个文件），加上测试和入口约 4,300 行；配合 ~2,000 行设计文档，整体精读量在 6,000 行左右。** 对一个高性能 IM Hub 来说，这个规模很克制，精读性价比很高。

优先啃 **reactor.cpp + queue.hpp + bridge/router/distributor** 五条线，再对照 `bridge_downlink_test` 和 `hub_proxy_test` 把双平面和路由串起来。

**第二遍必读**：[跟一条消息读代码.md](./跟一条消息读代码.md) — 按函数顺序跟一条上行业务帧打卡源码。

**故事线串框架**：[群聊故事线串 hi-im 核心业务 + hi-im-core 线程模型](./群聊故事线串起来hi-im核心业务+hi-im-core的线程模型辅助迅速掌握hi-im.md) — 11 步群聊 ↔ 四角色四队列 ↔ 源码锚点。
