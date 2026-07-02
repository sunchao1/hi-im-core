# hi-file-transfer 技术设计文档

> **组件**：hi-file-transfer（C++17/20）  
> **业务来源**：优信二手车贷后向合作银行（如新网银行）推送客户档案的真实场景抽象  
> **版本**：v0.1 · 2026-07-01  
> **状态**：方案设计（待实现）· 与 [hi-im-core 技术设计文档](技术设计文档.md) 并列，**独立仓库/独立二进制**  
> **许可证**：[Apache License 2.0](../LICENSE)

---

## 1. 背景与定位

### 1.1 真实业务来源（优信贷后 · 口述可验证）

优信二手车 **贷后** 环节需向合作银行推送贷款客户材料，包括但不限于：

- 身份证、征信、购车合同、车辆扫描件（**海量小图 + 数十 MB 级 PDF**）
- **视频面签** 等音视频文件（体积大、传输慢）
- 每日 **批量、万级** 任务，对接银行前置（如 **新网银行** SFTP/文件网关）

**当时线上实现（2017–2019，PHP 栈）：**

- 业务侧 PHP 组装任务，经 **SFTP/银行指定接口** 推送客户资料
- 曾用 **Swoole** 优化部分慢接口与并发，但 **大文件 IO、断点、对账、专线带宽** 仍是痛点
- 与 **RabbitMQ** 事件流、**Elasticsearch** 检索等贷中贷后系统并存

**hi-file-transfer 的定位：**

> 在上述 **真实痛点** 基础上，设计一套 **C++ 高性能、可分布式扩展** 的文件传输系统，作为 **个人/net-new 实现**（非声称优信当时已上线该 C++ 系统）。  
> 面试话术：**「优信做过 PHP 版银行材料推送；hi-file-transfer 是我把这条链路按 C++ 基础设施标准重做。」**

### 1.2 是什么

**hi-file-transfer** 是面向 **金融/企业跨边界文件投递** 的传输平台：

| 角色 | 说明 |
|------|------|
| **Coordinator（`hi-file-coord`）** | 任务接入、调度、分片元数据、对账、管控 API |
| **Transfer Worker（`hi-file-worker`）** | 数据面：读盘、分片、压缩、SFTP/HTTPS 推送、断点续传 |
| **Bank Adapter** | 按银行渠道插件化（SFTP 优先；预留 HTTPS/行方 SDK） |
| **Audit Store** | 全链路审计、任务状态、监管对账 |

与 **hi-im-core** 的分工：

| 系统 | 场景 | 数据特征 | 持久化 |
|------|------|----------|--------|
| **hi-im-core** | IM 进程间 **热路径消息** | 小包、高 QPS、内存转发 | 不持久化 |
| **hi-file-transfer** | **大文件/批量档案** 推银行 | MB～GB、分片、MD5 | 强持久化 + 审计 |

二者 **不共用 wire 协议、不共用二进制**；简历上作为 **两个 C++ 项目** 并列。

### 1.3 设计目标

| 维度 | 指标 |
|------|------|
| **吞吐** | 单 Worker 千兆内网下，**≥ 800 Mbps 有效 payload**（可配置压缩后按字节计） |
| **可靠性** | 分片级断点续传；任务级幂等；失败阶梯重试 |
| **合规** | 全局/分片 MD5；全链路审计；DMZ 单向外推；**不对银行声称自研协议替代 SFTP** |
| **扩展** | Coordinator + N Worker 水平扩展；按 `bank_id` 专线限流 |
| **工程** | C++17/20、CMake、单测、Docker Compose 验收、Prometheus |

### 1.4 非目标（Non-Goals）

- 不做 IM、不做实时消息总线（见 hi-im-core）
- 不做 P2P 端到端直传；**仅我方主动推银行前置**
- 第一期不做 Qt 运维客户端（HTTP 管控 + 日志检索即可）
- 第一期不替代银行侧接收逻辑（Mock SFTP Server 用于集成测试）

---

## 2. 系统架构

### 2.1 部署拓扑（金融隔离）

```text
┌─────────────────────────────────────────────────────────────┐
│ 优信内网 / 业务 VPC                                          │
│  贷后 PHP/Go 业务 ──► RabbitMQ/Kafka 任务                    │
│         │                                                    │
│         ▼                                                    │
│  hi-file-coord（调度 + 元数据 + HTTP 管控）                   │
│         │ gRPC / 内网 TCP 控制面                             │
│         ▼                                                    │
│  hi-file-worker × N（DMZ 区，仅出站）                         │
└──────────────────────────┬──────────────────────────────────┘
                           │ TLS + SFTP（主动连接）
                           ▼
              ┌────────────────────────────┐
              │ 合作银行前置（新网银行等）   │
              │ SFTP / 行方文件网关        │
              └────────────────────────────┘
```

**约束（与真实银行对接一致）：**

- Worker 部署 **DMZ**，**仅主动向外**建立 SFTP/TLS 连接
- 银行侧 **不能** 主动访问内网 Coordinator
- 原始客户文件 **AES 静态加密** 落盘；专线 **TLS 1.2+**

### 2.2 逻辑分层

```text
接入层     HTTP/gRPC 管控 · MQ 消费任务 · 幂等去重
调度层     优先级队列 · bank 维度并发/带宽令牌桶 · Worker 选路
数据面     mmap/read · 分片 · zstd(可选) · SFTP PUT · 断点状态机
持久层     SQLite/LevelDB 分片进度 · MySQL 对账归档 · 对象路径索引
可观测     spdlog 审计 · Prometheus · 告警 Webhook
```

### 2.3 分布式模型

| 概念 | 说明 |
|------|------|
| **Task** | 一次「客户档案推送」：customer_id、file_list、target_bank、priority |
| **Chunk** | 大文件切分单元（默认 8MB，可配置） |
| **Lease** | Worker 向 Coord 领取 Task/Chunk 的租约，防止重复执行 |
| **bank_id** | 银行渠道；独立限流、独立 SFTP 连接池 |
| **checkpoint** | 已成功 Chunk  bitmap，持久化后支持断点 |

**扩展规则：**

- Coordinator **无状态可主备**；元数据在 MySQL + 本地 LevelDB/SQLite
- Worker **水平加机器**；同一 Task 的 Chunk 可并行到多 Worker（Phase 2）
- M1：**单 Coordinator + 单 Worker** 即可验收

---

## 3. 核心流程

### 3.1 任务生命周期

```text
1. 业务投递 Task（MQ 或 HTTP）
2. Coord 校验路径、算全局 MD5、登记 Task=PENDING
3. Worker PullTask / Coord PushAssign
4. 预处理：小文件批量 tar.zst；大文件分 Chunk
5. 对每个 Chunk：SFTP STOR + 分片 MD5 回执
6. 全部分片 OK → 触发银行侧合并钩子（或整包上传）→ 全局 MD5 对账
7. Task=SUCCESS，写审计；失败则重试或 ALERT
```

### 3.2 断点续传

- 每 Chunk 成功：`checkpoint[chunk_id]=DONE` 写入 **LevelDB**（Worker 本地）并 **异步上报 Coord**
- 进程重启 / 网络闪断：扫描 checkpoint，**仅重传未完成 Chunk**
- 幂等键：`task_id + chunk_id + bank_id`；银行侧同名文件策略由 Adapter 配置（覆盖/版本后缀）

### 3.3 与优信 PHP 版的对应关系（面试用）

| 优信当时（PHP/Swoole） | hi-file-transfer（C++ 设计） |
|------------------------|------------------------------|
| PHP 脚本 + SFTP 扩展推文件 | Worker + libssh2 SFTP 连接池 |
| Swoole 提升接口并发 | epoll Worker + 多连接并行 Chunk |
| 大视频/ PDF 慢、易卡死 | 分片 + 断点 + 带宽令牌桶 |
| 手工对账/日志分散 | 结构化审计 + MySQL 日终对账表 |
| 单机为主 | Coordinator + Worker 分布式 |

---

## 4. 传输与 IO 设计

### 4.1 文件 IO

| 场景 | 策略 |
|------|------|
| 超大 PDF / 视频面签 | **mmap** 读；按 Chunk 偏移 `send`；避免整文件进内存 |
| 海量小图 | 打包 **tar + zstd** 再上传，减少 SFTP 会话次数 |
| 热路径优化 | 缓冲区池；文件句柄复用；可选 **read + TLS**（sendfile 与 TLS 并用需 kTLS 或放弃全程 sendfile） |

### 4.2 对外协议（银行侧）

**M1 强制：SFTP（libssh2）**，与真实优信/新网银行场景一致。

**对内控制面（Coord ↔ Worker）：**

- **gRPC**（Protobuf）：`AssignTask`、`ReportChunk`、`Heartbeat`、`ListBanks`
- 或 **轻量 TCP + Protobuf 帧**（与 hi-im-core 无关的 **file_ctrl v1** 头，见下节）

**禁止对外宣传：**「自研 TCP 替代银行 SFTP」—— 仅 **内部** 可用自定义帧在 Coord/Worker 间传 **元数据**，**文件字节** 走 SFTP。

### 4.3 file_ctrl v1（内网控制帧 · 草案）

```text
┌──────────────────────────────────────┐
│ magic(4) "HIFT" │ ver(2) │ type(2)   │
│ task_id(8)      │ chunk_id(4)        │
│ payload_len(4)  │ crc32(4)           │
├──────────────────────────────────────┤
│ Protobuf payload                     │
└──────────────────────────────────────┘
```

与 hi-im-core **20B wire** 完全独立，避免简历/面试混淆。

---

## 5. 线程与模块（Worker）

继承 hi-im-core 上验证过的模式，但 **载荷为大文件 IO**：

```text
┌──────────────┐
│ MQ Consumer  │ 或 gRPC Pull
└──────┬───────┘
       ▼
┌──────────────┐     ┌─────────────┐
│ Scheduler    │────►│ Chunk Reader│ mmap / pread
└──────┬───────┘     └──────┬──────┘
       ▼                    ▼
┌──────────────┐     ┌─────────────┐
│ SFTP Pool    │◄────│ Compress    │ zstd optional
│ (epoll)      │     └─────────────┘
└──────────────┘
       ▼
┌──────────────┐
│ Checkpoint   │ LevelDB
└──────────────┘
```

| 模块 | 职责 |
|------|------|
| `task_scheduler` | 优先级、bank 限流、租约 |
| `chunker` | 分片、MD5 |
| `sftp_adapter` | 连接池、STOR、断点偏移 |
| `compressor` | zstd 批量小文件 |
| `checkpoint_store` | LevelDB/SQLite |
| `audit_logger` | spdlog JSON 行 |

---

## 6. 安全与合规

| 项 | 实现 |
|----|------|
| 完整性 | 文件 MD5 + 分片 MD5；失败拒收 |
| 审计 | customer_id、bank、文件名、大小、起止时间、operator、node_id、status |
| 加密 | 静态 AES-256；传输 TLS；SFTP 密钥托管（KMS/配置中心 Phase 2） |
| 隔离 | DMZ Worker；管控 API 仅内网 |
| 幂等 | `business_batch_id` 去重 |

---

## 7. 代码目录规划（独立仓库）

```text
hi-file-transfer/
├── CMakeLists.txt
├── doc/                          # 可 symlink 至 hi-im-core/doc 相关篇
├── proto/                        # task.proto, control.proto
├── include/hift/
│   ├── coord/
│   └── worker/
├── src/
│   ├── coord/
│   ├── worker/
│   ├── adapter/sftp/
│   └── common/                   # checkpoint, md5, rate_limit
├── cmd/
│   ├── hi-file-coord/main.cpp
│   ├── hi-file-worker/main.cpp
│   └── hi-file-bench/main.cpp
├── test/
├── deploy/docker/
└── bench/baseline-v1.json
```

**与 hi-im-core 仓库关系：** 文档先放在 hi-im-core/doc/ 统一维护；**代码实现时建议独立 git 仓库**，避免 IM 与文件传输耦合。

---

## 8. 里程碑

| 阶段 | 内容 | 验收 |
|------|------|------|
| **M1** | 单 Coord + 单 Worker；SFTP 推 Mock；分片+断点+MD5 | 集成测试 + 1GB 断点续传 |
| **M2** | MQ 接入；MySQL 审计；HTTP 管控 | 端到端 Task 查询 |
| **M3** | 多 Worker；bank 限流；Prometheus | 2 Worker 压测报告 |
| **M4** | 音视频大文件策略；密钥轮换 | 与 M1 基线对比报告 |

---

## 9. 简历与面试（诚实表述）

### 9.1 两段经历如何并存

| 项目 | 简历写法 | 时间线 |
|------|----------|--------|
| **优信贷后材料推送** | PHP/Swoole/SFTP/新网银行联调；**真实在职** | 2017–2019 |
| **hi-file-transfer** | C++ 分布式文件传输；**个人实现/重构设计**；业务源于贷后场景 | 2025–2026（实现中） |
| **hi-im-core** | C++ IM 消息总线；**已实现 M1** | 2025–2026 |

### 9.2 推荐简历 bullet（hi-file-transfer · 实现后使用）

```text
hi-file-transfer | C++17 高性能分布式文件传输（个人项目，业务抽象自顾信贷后推银行）
· Coordinator/Worker 分离；分片、断点续传、双层 MD5；SFTP 适配新网银行等前置网关
· epoll 连接池 + mmap 读大文件；LevelDB checkpoint；RabbitMQ 任务接入
· 解决 PHP 时代大 PDF/视频面签上传慢、难对账痛点；与 hi-im-core（小包总线）场景互补
```

### 9.3 面试官常问 · 标准答法

- **Q：优信当时不就是 PHP 吗？**  
  A：是，贷后推银行材料是 PHP + SFTP；大文件和音视频面签慢，Swoole 只能缓解接口并发。hi-file-transfer 是我用 C++ 把 **传输内核** 按基础设施标准重做，**不是**声称优信当年已上线这套 C++。

- **Q：和 hi-im-core 什么关系？**  
  A：无关。一个是 **IM 小包内存总线**，一个是 **大文件推银行**；共用 epoll/工程化经验，代码分开。

- **Q：为什么不用 hi-im-core 传文件？**  
  A：hub 不持久化、面向 256B 级热路径；传 GB 级视频会撑爆队列且不合规审计要求。

---

## 10. 参考

- 优信业务背景（个人经历）：贷后、新网银行、客户材料、视频面签
- [hi-im-core 技术设计文档](技术设计文档.md) — 仅作 **并发模型/工程** 对照，非代码依赖
- beehive-im `doc/personal/优信-银行金融客户材料高性能文件传输系统.md` — 早期大纲，本文档为 **可落地 v0.1**

---

*文档版本：2026-07-01 · 待 M1 编码实现。*
