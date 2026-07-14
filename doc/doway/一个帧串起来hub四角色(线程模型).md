# 一个真实帧串起来 Hub 四角色（线程模型）

> **用途**：拿一帧 `hi-im-gateway` 真实 TCP 写出数据，按 **11 步群聊全链路** 对齐 **Listener / Reactor / Worker / Distributor** 四角色，并贴上关键 C++ 源码。  
> **前置**：[群聊的订阅关系梳理.md](./群聊的订阅关系梳理.md) §5、[群聊故事线串起来…](./群聊故事线串起来hi-im核心业务+hi-im-core的线程模型辅助迅速掌握hi-im.md)  
> **抓包来源**：gateway（NID=20001）→ Hub **FORWARD** 业务上行帧

---

## 0. 先看清这帧到底是什么

### 0.1 原始 hex（共 89 字节 = 20 + 69）

```text
# WireHeader 20B
0000030b 00004e21 00000001 00000045 1fe23dc4

# Payload 69B = IM Header 52B + body 17B
0000030b 00000011 00000000000186a1 000000000000002a
00004e21 000000000000006a 00000000000000000000000000000000
08a18d0610042a0968692c667269656e64
```

### 0.2 WireHeader（Hub 真正路由认这层）

| 偏移 | 字段 | hex | 主机序值 | 含义 |
|------|------|-----|----------|------|
| 0 | type | `0000030b` | **0x030B** | GROUP-CHAT |
| 4 | nid | `00004e21` | **20001** | 发送方 gateway-1 的 NID |
| 8 | flag | `00000001` | **1 (EXP)** | 业务帧（非 AUTH/SUB） |
| 12 | length | `00000045` | **69** | payload 长度 |
| 16 | chksum | `1fe23dc4` | 魔数 | 合法帧校验 |

协议定义：

```32:49:include/hiim/wire/header.hpp
static constexpr uint32_t kWireChecksum = 0x1FE23DC4u;
static constexpr uint32_t kFlagSys = 0;
static constexpr uint32_t kFlagExp = 1;
static constexpr std::size_t kWireHeaderSize = 20;

#pragma pack(push, 1)
struct WireHeader {
  uint32_t type;    // 命令字：SysCmd 或业务 cmd
  uint32_t nid;     // 节点 ID，路由与认证绑定用
  uint32_t flag;    // kFlagSys / kFlagExp，区分系统帧与业务帧
  uint32_t length;  // payload 字节长度（不含帧头）
  uint32_t chksum;  // 固定魔数 kWireChecksum
};
#pragma pack(pop)
```

### 0.3 IM Header（payload 前 52B；Hub 上行几乎不读，下行 bridge 才读 dest_nid）

| 偏移（相对 payload） | 字段 | 值 |
|----------------------|------|-----|
| 0 | cmd | 0x030B |
| 4 | length | **17**（body 长度） |
| 8 | sid | **100001**（甲的用户/会话 id） |
| 16 | cid | **42** |
| 24 | nid / dest_nid | **20001** |
| 28 | seq | **106** |

```33:40:include/hiim/im/header.hpp
static constexpr std::size_t kHeaderSize = 52;
static constexpr std::size_t kOffsetCmd = 0;
static constexpr std::size_t kOffsetLength = 4;
static constexpr std::size_t kOffsetSid = 8;
static constexpr std::size_t kOffsetCid = 16;
static constexpr std::size_t kOffsetNid = 24;     // bridge 下行路由关键字段
static constexpr std::size_t kOffsetSeq = 28;
```

### 0.4 Body（17B Protobuf，Hub 当 opaque）

```text
08 a1 8d 06 10 04 2a 09 68 69 2c 66 72 69 65 6e 64
                         └─ length-delimited "hi,friend"
```

| 项 | 值 |
|----|-----|
| 文本 | **`hi,friend`**（9 字节；本抓包无感叹号） |
| Hub 是否解析 | **否**（不碰 PB） |

### 0.5 结构示意

```text
┌──────────────────────────────── Wire 帧 89B ───────────────────────────────┐
│ WireHeader 20B │                    Payload 69B                             │
│ type=0x030B    │ ┌──────── IM 52B ────────┐┌──── body 17B ────┐             │
│ nid=20001      │ │ cmd=0x030B sid=100001  ││ … "hi,friend" … │             │
│ flag=EXP       │ │ dest_nid=20001 seq=106 │└──────────────────┘             │
│ length=69      │ └────────────────────────┘                                  │
│ chksum=魔数    │                                                              │
└────────────────┴─────────────────────────────────────────────────────────────┘
```

### 0.6 这帧在 11 步里处在哪

| 范围 | 是否是**本帧字节** | 说明 |
|------|-------------------|------|
| 步骤 1–2 | 否 | 浏览器 WS → gateway 封装成本帧 |
| **步骤 3–5** | **是** | **本 hex 原样进入 FORWARD，再 Publish 到 msgsvr TCP** |
| 步骤 6 | 否 | msgsvr Go 解析 PB；Hub 已结束对本帧上行的任务 |
| 步骤 7–9 | **新帧** | msgsvr 再发 1～2 帧 AsyncSend（`dest_nid=20001/20002`） |
| 步骤 10–11 | 否 | gateway WS 下行 |

> **读法**：下文用 **本帧** 把上行 3–5 走透；步骤 7–9 用「msgsvr 新造的下行帧」把同一套四角色再走一遍。

---

## 1. 热路径之前：四角色已就位（本帧不碰 Listener）

网关 TCP 早已 AUTH，`BindNid(20001)` 已写入 FORWARD NID 表；msgsvr 已在 BACKEND `SUB(0x030B)`。

本业务帧路径里：

| 角色 | 本帧上行会不会碰到 | 原因 |
|------|-------------------|------|
| **Listener** | ❌ | accept 早做完了，connq 不入业务帧 |
| **Reactor** | ✅ | 收 TCP、拼帧、入 recvq；以及写出 |
| **Worker** | ✅ | bridge Publish / AsyncSend |
| **Distributor** | ✅ | distq → sendq |

AUTH 绑定（步骤 0，系统帧，非本帧）：

```256:263:src/hub/reactor.cpp
      session.authed = true;
      session.gid = hiim::wire::BeToHost32(auth.gid);
      session.nid = hiim::wire::BeToHost32(auth.nid);
      // 绑定 nid → (sid, reactor_idx) 供后续 AsyncSend 路由
      ctx_.GetRouter().BindNid(session.nid, session.sid, idx_);
      const auto ack = EncodeFrame(static_cast<uint32_t>(SysCmd::kAuthAck), session.nid,
                                   kFlagSys, std::span<const uint8_t>{});
      SendBytes(session, ack);
```

msgsvr SUB（步骤 0，系统帧）：

```266:278:src/hub/reactor.cpp
    case SysCmd::kSubReq: {
      // ...
      const uint32_t cmd = hiim::wire::BeToHost32(cmd_be);
      Subscriber sub{session.gid, session.sid, session.nid, idx_};
      ctx_.GetRouter().Subscribe(cmd, sub);
      const auto ack = EncodeFrame(static_cast<uint32_t>(SysCmd::kSubAck), session.nid,
                                   kFlagSys, std::span<const uint8_t>{});
      SendBytes(session, ack);
```

---

## 2. 步骤 1–2：浏览器 → gateway（Hub 管不到）

```text
甲浏览器 --WS--> gateway-1(NID=20001)
gateway hubclient 组装上面 89B，write 到 FORWARD :28888
```

Hub 这侧随后见到的，就是本文 hex。

---

## 3. 步骤 3：FORWARD · Reactor —— 拼帧 → recvq

### 3.1 线程模型

```text
角色：Reactor（FORWARD，某 idx）
队列：recvq[worker]  ← Push（MPSC）
唤醒：WorkerWakeup.Notify()
```

### 3.2 epoll 可读 → recv → Append → TryPopFrame

```343:372:src/hub/reactor.cpp
void Reactor::HandleReadable(int fd) {
  // 唤醒 fd：Drain pipe/eventfd 后处理 ConnQueue 和 SendQueue
  if (fd == ctx_.ReactorWakeup(idx_).Fd()) {
    ctx_.ReactorWakeup(idx_).Drain();
    DrainNewConnections();
    DrainSendQueue();
    return;
  }
  // ...
  while (true) {
    const ssize_t n = recv(session.fd, buf, sizeof(buf), 0);
    if (n > 0) {
      session.fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
      while (auto frame = session.fb.TryPopFrame()) {
        const uint32_t flag = HeaderFieldHost(frame->header, &WireHeader::flag);
        if (flag == kFlagSys) {
          if (!HandleSystem(session, *frame)) {
            return;
          }
        } else {
          EnqueueInbound(session, std::move(*frame));
        }
      }
```

对本帧：

- `flag == 1`（EXP）→ **不走** `HandleSystem`
- 走 `EnqueueInbound`

拼帧依据：头 20B + `length=69` ⇒ 帧长 89B，且 chksum=`0x1FE23DC4`：

```71:89:src/wire/frame_buffer.hpp
  std::optional<FrameView> TryPopFrame() {
    if (buf_.size() < kWireHeaderSize) {
      return std::nullopt;
    }
    WireHeader hdr{};
    if (!DecodeHeader(std::span<const uint8_t>(buf_.data(), buf_.size()), hdr)) {
      buf_.clear();
      return std::nullopt;
    }
    const uint32_t payload_len = HeaderFieldHost(hdr, &WireHeader::length);
    const std::size_t frame_len = kWireHeaderSize + payload_len;
    if (buf_.size() < frame_len) {
      return std::nullopt;
    }
```

### 3.3 EnqueueInbound：业务帧进 recvq

```305:340:src/hub/reactor.cpp
void Reactor::EnqueueInbound(Session& session, FrameView frame) {
  if (!session.authed) {
    CloseSession(session.sid);
    return;
  }
  InboundMessage msg{};
  msg.sid = session.sid;
  msg.reactor_idx = idx_;
  msg.gid = session.gid;
  msg.nid = session.nid;
  msg.type = HeaderFieldHost(frame.header, &WireHeader::type);  // → 0x030B
  msg.flag = HeaderFieldHost(frame.header, &WireHeader::flag);  // → EXP

  const int worker_idx = PickWorker(session.sid);
  // ... 打日志：wire_nid=20001 im_dest_nid=20001 seq=106 cmd=0x30b ...

  msg.payload = std::move(frame.payload);  // 69B，原样

  auto& q = ctx_.RecvQueue(worker_idx);
  if (!PushWithBackoff(q, std::move(msg))) {
    // 队列满则丢
    return;
  }
  ctx_.WorkerWakeup(worker_idx).Notify();
}
```

对本帧填入的 `InboundMessage`：

| 字段 | 值 |
|------|-----|
| type | `0x030B` |
| flag | `1` |
| nid | session.nid（AUTH 绑的 20001） |
| payload | **本 hex 后 69 字节**（IM+body） |

```text
【步骤 3 小结】
  Reactor 线程：recv → 拼出 89B 完整帧
  → Push 到 recvq[worker_idx]（MPSC）
  → Notify Worker
  Listener / Distributor：不参与
```

---

## 4. 步骤 4：FORWARD · Worker —— bridge Publish

### 4.1 线程模型

```text
角色：Worker（FORWARD）
队列：recvq ← Pop
下一跳：peer(BACKEND)->Publish → BACKEND distq
```

### 4.2 Worker 分发 handler

```78:111:src/hub/worker.cpp
void Worker::Run() {
  auto& q = ctx_.RecvQueue(idx_);
  while (ctx_.Running().load(std::memory_order_acquire)) {
    // ... epoll 等待 WorkerWakeup ...
    while (auto msg = q.Pop()) {
      MessageHandler handler = ctx_.FindHandler(msg->type);  // 通常没有 0x030B
      if (!handler) {
        handler = ctx_.FindHandler(0);  // bridge 默认 handler
      }
      if (handler) {
        handler(ctx_, *msg);
      }
    }
  }
}
```

Hub **没有**业务「群聊 handler」；`0x030B` 找不到后 fallback 到 **cmd=0 的 bridge**。

### 4.3 ForwardUplinkHandler：整包 Publish 到 BACKEND

```43:51:src/hub/bridge.cpp
void ForwardUplinkHandler(HubContext& ctx, const InboundMessage& msg) {
  HubContext* peer = ctx.Peer();  // BACKEND HubContext*
  if (peer == nullptr) {
    return;
  }
  // 关键：payload 原样；路由键是 msg.type=0x030B，不是 IM.dest_nid
  const Status st = peer->Publish(msg.type, msg.payload.data(), msg.payload.size());
  if (!st.ok() && st.code != StatusCode::kNotFound) {
    std::cerr << "[bridge] forward publish failed: " << st.message << "\n";
  }
}
```

对本帧：

```text
peer->Publish(
  cmd  = 0x030B,
  data = 69B payload（IM 头 sid=100001 + body "hi,friend"）
)
```

**上行不读 `IM.dest_nid`。** 即使本帧 dest_nid=20001，bridge 上行也不用它。

```text
【步骤 4 小结】
  Worker 线程：recvq.Pop → FindHandler(0) → ForwardUplinkHandler
  → BACKEND.Publish(0x030B, 69B)
  Reactor 不在这里 send；Distributor 还未上
```

---

## 5. 步骤 5：BACKEND · Publish → Distributor → Reactor 写出 msgsvr

这一步把 **同一套四角色再跑半圈（出站半圈）**：

```text
Publish（在 bridge/Worker 调用栈上执行） → distq
→ Distributor Pop → sendq
→ Reactor DrainSendQueue → send(msgsvr TCP)
```

### 5.1 Publish：查 SUB 表 → 入 BACKEND distq

```120:141:src/hub/context_impl.cpp
Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len) {
  const auto subs = ctx.GetRouter().FindSubscribers(cmd);  // cmd=0x030B
  if (subs.empty()) {
    return Status::Error(StatusCode::kNotFound, "no subscribers");
  }
  for (const auto& sub : subs) {
    // 重新封成 wire 帧；wire.nid = 订阅者 nid（msgsvr=30001）
    const auto frame =
        hiim::wire::EncodeFrame(cmd, sub.nid, hiim::wire::kFlagExp,
                                std::span<const uint8_t>(data, len));
    OutboundFrame out{};
    out.sid = sub.sid;
    out.reactor_idx = sub.reactor_idx;
    out.bytes = std::move(frame);
    if (!PushWithBackoff(ctx.DistQueue(), std::move(out))) {
      return Status::Error(StatusCode::kQueueFull);
    }
  }
  ctx.DistWakeup().Notify();
  return Status::Ok();
}
```

对本抓包场景，SUB 表典型为：

```text
0x030B → [ { nid=30001, sid=msgsvr_sid, reactor_idx=k } ]
```

Publish **再编码** 出去的帧会变成：

```text
WireHeader:
  type=0x030B
  nid =30001     ← 不再是 20001，而是订阅者 msgsvr 的 NID
  flag=EXP
  length=69
  chksum=魔数
Payload: 仍是原来的 69B（IM+"hi,friend"）
```

### 5.2 Distributor：distq → sendq

```108:137:src/hub/distributor.cpp
void Distributor::Run() {
  auto route_frame = [this](OutboundFrame frame) {
    RouteToSendQueue(ctx_, std::move(frame));
  };
  while (ctx_.Running().load(std::memory_order_acquire)) {
    // DistWakeup ...
    while (auto frame = ctx_.DistQueue().Pop()) {
      route_frame(std::move(*frame));
    }
  }
}
```

```47:63:src/hub/distributor.cpp
bool RouteToSendQueue(HubContext& ctx, OutboundFrame frame) {
  // ...
  auto& sendq = ctx.SendQueue(reactor_idx);
  if (!PushWithBackoff(sendq, std::move(frame))) {
    LogDistributorRouteFail(log_frame, meta, "send queue full");
    return false;
  }
  LogDistributorRouteOk(log_frame, meta);
  ctx.ReactorWakeup(reactor_idx).Notify();
  return true;
}
```

### 5.3 BACKEND Reactor：sendq → TCP 写 msgsvr

```169:191:src/hub/reactor.cpp
void Reactor::DrainSendQueue() {
  auto& q = ctx_.SendQueue(idx_);
  while (auto item = q.Pop()) {
    const auto it = sessions_.find(item->sid);
    if (it == sessions_.end()) {
      // session 已断则丢
      continue;
    }
    SendBytes(it->second, item->bytes);
  }
}

bool Reactor::SendBytes(Session& session, std::span<const uint8_t> data) {
  session.outbuf.insert(session.outbuf.end(), data.begin(), data.end());
  UpdateInterest(session, true, true);
  HandleWritable(session.fd);  // 非阻塞 send
  return true;
}
```

```text
【步骤 5 小结 · 本上行帧在 Hub 内的终点】
  Publish 查 SUB → 编码 wire.nid=30001 的出站帧
  → BACKEND distq（MPSC）
  → Distributor 投 sendq[k]（SPSC）
  → BACKEND Reactor send → msgsvr hubclient TCP
  payload 里的 "hi,friend" 原样抵达 msgsvr
```

---

## 6. 步骤 6：msgsvr（Hub 外）—— 本帧故事在 Hub 侧告一段落

msgsvr 的 hubclient 收 TCP 帧 → **本地 Go handler**（不是 Hub Worker handler）→ 解析 PB `"hi,friend"`、写库、Redis：

```text
gid → { 100001@gateway 20001, 100002@gateway 20002 }
```

**Hub 的四角色线程此时不跑 msgsvr 业务逻辑。**

---

## 7. 步骤 7–9：下行再跑一遍四角色（新帧，非原文 hex）

msgsvr 为每个成员 gateway 发 **新的** hubclient 帧到 BACKEND，例如给乙：

```text
Wire: type=0x030B, nid=30001(msgsvr), flag=EXP
IM:   dest_nid=20002, seq=…, body 仍含 "hi,friend"
```

以及给甲回显：`dest_nid=20001`。

### 7.1 步骤 7：BACKEND Reactor → recvq → Worker（同步骤 3 模型）

与步骤 3 相同代码路径：`HandleReadable` → `EnqueueInbound` → `WorkerWakeup`。

### 7.2 步骤 8：BACKEND Worker → BackendDownlinkHandler

```57:72:src/hub/bridge.cpp
void BackendDownlinkHandler(HubContext& ctx, const InboundMessage& msg) {
  HubContext* peer = ctx.Peer();  // FORWARD
  const uint32_t dest_nid = ReadImDestNid(msg);  // ← 读 IM offset 24
  const uint64_t im_seq = hiim::im::ReadSeq(msg.payload);
  // ...
  const Status st =
      peer->AsyncSend(msg.type, dest_nid, msg.payload.data(), msg.payload.size());
  LogBridgeAsyncSend(dest_nid, im_seq, msg.type, st);
}
```

```88:96:include/hiim/im/header.hpp
inline uint32_t ReadDestNid(std::span<const uint8_t> payload) {
  if (payload.size() < kHeaderSize) {
    return 0;
  }
  uint32_t be_nid = 0;
  std::memcpy(&be_nid, payload.data() + kOffsetNid, sizeof(be_nid));
  return hiim::wire::BeToHost32(be_nid);
}
```

对照：**若把「本上行帧」误用做下行，IM.dest_nid 也是 20001**——那只是「回显到甲 gateway」的候选；真正跨端到乙必须是 msgsvr 写的 `dest_nid=20002` 新帧。

### 7.3 步骤 8→9：AsyncSend 查 NID 表 → FORWARD distq → Distributor → Reactor

```147:167:src/hub/context_impl.cpp
Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                 const uint8_t* data, std::size_t len) {
  const auto route = ctx.GetRouter().FindNidRoute(dest_nid);  // 20002 → gateway-2
  if (!route.has_value()) {
    return Status::Error(StatusCode::kNotFound, "nid not connected");
  }
  const auto frame = hiim::wire::EncodeFrame(cmd, dest_nid, hiim::wire::kFlagExp,
                                             std::span<const uint8_t>(data, len));
  OutboundFrame out{};
  out.sid = route->sid;
  out.reactor_idx = route->reactor_idx;
  out.bytes = std::move(frame);
  if (!PushWithBackoff(ctx.DistQueue(), std::move(out))) {
    return Status::Error(StatusCode::kQueueFull);
  }
  ctx.DistWakeup().Notify();
  return Status::Ok();
}
```

之后与步骤 5 相同：`Distributor::Run` → `RouteToSendQueue` → `DrainSendQueue` → TCP 写到 gateway-2 / gateway-1。

```text
【下行小结】
  BACKEND: Reactor→recvq→Worker→bridge.AsyncSend
  FORWARD: Publish/AsyncSend入distq→Distributor→sendq→Reactor→gateway TCP
  路由键：IM.dest_nid（与上行「按 cmd SUB」不同）
```

---

## 8. 步骤 10–11：gateway WS（Hub 外）

gateway 收 hubclient 下行帧 → 按 uid↔WS 推甲 / 乙浏览器。Hub 四角色到此结束。

---

## 9. 总表：本帧（上行）+ 下游新帧，四角色对照

| 11 步 | 平面 | 角色 | 队列 | 对本抓包 hex |
|------|------|------|------|-------------|
| 1–2 | — | — | — | gateway 产出本帧 |
| **3** | FORWARD | **Reactor** | → **recvq** | **消费本 89B** |
| **4** | FORWARD | **Worker** + bridge | — | **Publish(payload=69B)** |
| **5** | BACKEND | Publish + **Distributor** + **Reactor** | distq→sendq | **payload 原样到 msgsvr**；wire.nid 变为 30001 |
| 6 | — | — | — | Go handler 解析 `"hi,friend"` |
| 7 | BACKEND | **Reactor** → **Worker** | → recvq | **新帧**（msgsvr 写出） |
| 8 | BACKEND | **Worker** + bridge | → FORWARD distq | `ReadDestNid` + AsyncSend |
| 9 | FORWARD | **Distributor** + **Reactor** | distq→sendq | 推 gateway NID |
| 10–11 | — | — | — | WS 推浏览器 |

### 9.1 一张「本帧在 Hub 内」的流水线

```text
gateway TCP 写出本 hex
        │
        ▼
┌─ FORWARD Reactor ──────────────────────────────┐
│  HandleReadable → FrameBuffer(89B)             │
│  flag=EXP → EnqueueInbound                     │
│  Push recvq + WorkerWakeup                     │
└──────────────────────┬─────────────────────────┘
                       │ recvq
                       ▼
┌─ FORWARD Worker ───────────────────────────────┐
│  Pop → FindHandler(0) → ForwardUplinkHandler   │
│  peer.Publish(0x030B, 69B payload)             │
└──────────────────────┬─────────────────────────┘
                       │ BACKEND DistQueue
                       ▼
┌─ BACKEND Distributor ──────────────────────────┐
│  Pop distq → Push sendq[msgsvr_reactor]        │
│  ReactorWakeup                                 │
└──────────────────────┬─────────────────────────┘
                       │ sendq
                       ▼
┌─ BACKEND Reactor ──────────────────────────────┐
│  DrainSendQueue → SendBytes → TCP→msgsvr       │
└────────────────────────────────────────────────┘
```

### 9.2 Listener 为何全程缺席

```text
Listener 只做：accept → connq → Reactor DrainNewConnections
本帧是「已 stick 的 Session.fd」上的业务读写 → 不经 Listener / connq
```

---

## 10. 用日志对一下（你可以本地复现）

开启 Hub 后，本帧上行典型可见：

```text
[reactor] enqueue inbound ... wire_nid=20001 im_dest_nid=20001 seq=106 cmd=0x30b
[hub] publish enqueue plane=backend sub_nid=30001 ... seq=106 cmd=0x30b
[distributor] route ok ... wire_nid=30001 im_dest_nid=20001 seq=106 cmd=0x30b
```

下行（msgsvr fan-out 后）：

```text
[bridge] backend recv downlink dest_nid=20002 seq=... cmd=0x30b
[hub] async_send enqueue plane=forward dest_nid=20002 ...
[distributor] route ok ...
```

---

## 11. 自检题（对着本 hex）

1. 本帧 `wire.nid` 与 `IM.dest_nid` 都是 20001，上行 bridge 用哪个做路由？  
   → **都不用做路由键；上行用 `type=0x030B` 查 SUB。**
2. Publish 之后 TCP 上看到的 wire.nid 变成什么？  
   → **订阅者 msgsvr 的 30001。**
3. `"hi,friend"` 第一次被谁解析？  
   → **msgsvr Go handler；Hub 只搬运 opaque payload。**
4. 本 hex 会直接出现在 gateway-2 的 TCP 上吗？  
   → **不会原样「整网透传 wire.nid=20001」；下行是 msgsvr 重发、bridge/AsyncSend 再编码的新帧。**
5. 四个队列里谁处理本上行帧？  
   → **recvq（FORWARD）→ distq（BACKEND）→ sendq（BACKEND）。connq 不碰。**

---

## 12. 关联文档与源码索引

| 文档 / 文件 | 作用 |
|-------------|------|
| [群聊的订阅关系梳理.md](./群聊的订阅关系梳理.md) | 11 步 + SUB/NID/Redis/WS |
| [群聊故事线串起来…](./群聊故事线串起来hi-im核心业务+hi-im-core的线程模型辅助迅速掌握hi-im.md) | 故事线 ↔ 线程模型总表 |
| `reactor.cpp` | 步骤 3 / 5·出站 / 7 |
| `worker.cpp` | 步骤 4 / 8 |
| `bridge.cpp` | 步骤 4 Publish / 8 AsyncSend |
| `context_impl.cpp` | Publish / AsyncSend |
| `distributor.cpp` | 步骤 5 / 9 的 distq→sendq |
| `frame_buffer.hpp` | 本帧 89B 拼帧 |
| `wire/header.hpp` / `im/header.hpp` | 本 hex 字段定义 |

---

## 13. 一句话

> **这段 gateway 写出的 89 字节，在 hi-im-core 里走的是：FORWARD `Reactor` 拼帧入 `recvq` → `Worker`+bridge `Publish(0x030B)` → BACKEND `distq` → `Distributor` → `sendq` → `Reactor` 写给 msgsvr；payload 里的 `hi,friend` Hub 从不解析。** 乙能收到，靠的是 msgsvr 再起 **第二轮** 四角色下行（`dest_nid=20002`），不是本上行帧直接透传。
