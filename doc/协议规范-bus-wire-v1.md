# hi-im bus wire v1 协议规范

> **适用范围**：hi-im-core Hub ↔ hi-im-proxy（Go）/ C++ Proxy  
> **兼容性**：与必嗨 RTMQ 线协议 **二进制兼容**  
> **版本**：v1 · 2026-06-25

---

## 1. 概述

bus wire v1 是 **进程间 TCP 消息帧**，不含 IM 业务语义。一帧结构：

```text
┌────────────────────────────────────────┐
│ Header (20 bytes, packed, network BO)  │
├────────────────────────────────────────┤
│ Payload (length bytes)                 │
└────────────────────────────────────────┘
```

业务 IM 帧在 Payload 内：`[ IM MesgHeader 48B | protobuf body ]`（由 gateway/msgsvr 解析，Hub 视 Payload 为 opaque）。

---

## 2. 报头结构（20 字节）

与必嗨 `rtmq_header_t` 字段顺序、大小、字节序 **完全一致**：

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | type | uint32 BE | 系统 cmd 或业务 cmd |
| 4 | nid | uint32 BE | 上行=源 NID；下行 async_send=目的 NID |
| 8 | flag | uint32 BE | 0=系统，1=业务 |
| 12 | length | uint32 BE | Payload 长度 |
| 16 | chksum | uint32 BE | 固定魔数 `0x1FE23DC4` |

C++ 定义（hi-im-core）：

```cpp
#pragma pack(push, 1)
struct WireHeader {
  uint32_t type;
  uint32_t nid;
  uint32_t flag;
  uint32_t length;
  uint32_t chksum;
};
#pragma pack(pop)

static constexpr uint32_t kWireChecksum = 0x1FE23DC4u;
static constexpr uint32_t kFlagSys = 0;
static constexpr uint32_t kFlagExp = 1;
```

**TCP 流式传输**：接收方用 snap 缓冲拼帧；`length + 20 == 帧长` 且 chksum 合法才投递。

---

## 3. flag 与 type

### 3.1 系统消息（flag = 0）

| type | 名称 | 方向 | 说明 |
|------|------|------|------|
| 0x0001 | AUTH_REQ | Proxy→Hub | 鉴权 |
| 0x0002 | AUTH_ACK | Hub→Proxy | |
| 0x0003 | KPALIVE_REQ | 双向 | 默认 30s |
| 0x0004 | KPALIVE_ACK | 双向 | |
| 0x0005 | SUB_REQ | Proxy→Hub | 订阅业务 cmd |
| 0x0006 | SUB_ACK | Hub→Proxy | |
| 0x1001 | QUERY_CONF_REQ | 管理 | 可选 |
| 0x1002 | QUERY_CONF_ACK | 管理 | |

Hub 内部线程命令（不暴露给 Proxy TCP）：ADD_SCK、DIST_REQ、PROC_REQ、SEND 等，见必嗨 `rtmq_mesg_e`；hi-im-core 实现等价语义，cmd 值保持一致便于对照。

### 3.2 业务消息（flag = 1）

`type` = hi-im-api 定义的 `CMD_*`（如 `0x030B` GROUP-CHAT）。Payload = 完整 IM 二进制帧（48B 头 + 体）。

---

## 4. 连接生命周期

```text
1. Proxy TCP connect Hub
2. AUTH_REQ { gid, user[32], pass[16], nid }
3. AUTH_ACK
4. 对每个要接收的 cmd：SUB_REQ { type }
5. SUB_ACK
6. 业务：Proxy AsyncSend → Hub；Hub 下行 → Proxy handler
7. 周期 KPALIVE_REQ / ACK
8. 断开：Hub 清理 SUB 与 nid 映射
```

### 4.1 AUTH_REQ Payload（与必嗨对齐）

| 字段 | 类型 | 说明 |
|------|------|------|
| gid | uint32 BE | 分组 |
| user | char[32] | 用户名 |
| passwd | char[16] | 密码 |
| nid | uint32 BE | 本 Proxy 进程 NID |

---

## 5. publish 与 async_send 在帧上的体现

| 操作 | 谁发起 | 帧特征 |
|------|--------|--------|
| Proxy 上行 | Proxy | flag=EXP, nid=**本进程 NID**, type=业务 cmd |
| Hub publish | Hub 内部 | 查 SUB 后多次 async_send |
| Hub async_send | Hub→Proxy | flag=EXP, nid=**目的 NID**, type=业务 cmd |

**bridge 下行**：从 BACKEND 收到 Payload，读 IM MesgHeader 中 **目的 nid**，再 async_send 到 FORWARD 平面。

---

## 6. 与 IM 48 字节头的关系

```text
[ WireHeader 20B | IM MesgHeader 48B | protobuf ]
 ↑ bus wire v1      ↑ hi-im-api/comm   ↑ 业务
```

- Hub **不解析** protobuf。
- bridge 仅在有需要时读 IM 头 **nid 字段**（偏移见 hi-im-api）。

---

## 7. 兼容性与版本演进

| 版本 | 说明 |
|------|------|
| **v1** | 当前；与 RTMQ 兼容 |
| **v2**（规划） | 可选扩展头；AUTH 协商 `wire_version`；未协商则 v1 |

hi-im-bench 与 hi-im-proxy **必须** 先实现 v1 再考虑 v2。

---

## 8. 对照清单（实现验收）

- [ ] Header 20 字节 packed，字段顺序与 `rtmq_mesg.h` 一致
- [ ] chksum 固定 `0x1FE23DC4`，不参与 CRC 计算
- [ ] 网络字节序大端
- [ ] AUTH/SUB/KPALIVE cmd 值与必嗨一致
- [ ] 半包/粘包/多帧 snap 单测通过
- [ ] 与必嗨 frwder 对接 hi-im-proxy 互通（Stage 兼容测试）

---

## 9. 参考

- 必嗨：`beehive-im/src/clang/incl/rtmq/rtmq_mesg.h`
- hi-im-core 实现：`include/hiim/wire/header.hpp`
