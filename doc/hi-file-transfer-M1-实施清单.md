# hi-file-transfer M1 实施清单

> **目标**：单 Coordinator + 单 Worker + Mock SFTP；分片、断点续传、MD5、Docker 验收  
> **工期**：约 3 周（兼职）  
> **前置**：阅读 [hi-file-transfer-技术设计文档.md](hi-file-transfer-技术设计文档.md)

---

## Week 1 · 骨架 + 分片/校验

### 1.1 工程

- [ ] 独立仓库 init（或 hi-im-core 同级 `hi-file-transfer/`）
- [ ] CMakeLists.txt（C++17、CTest、-Wall -Wextra）
- [ ] `proto/task.proto`：Task、Chunk、Checkpoint
- [ ] `.gitignore`、`LICENSE`（Apache-2.0）

### 1.2 公共库

- [ ] `src/common/md5.hpp` — 文件/分片 MD5
- [ ] `src/common/chunker.hpp` — 按 8MB 切分
- [ ] `test/chunker_test.cpp`、`test/md5_test.cpp`

**Week 1 验收**：`ctest` 分片与 MD5 单测全绿。

---

## Week 2 · Worker + SFTP + 断点

### 2.1 Worker 数据面

- [ ] `src/worker/checkpoint_store.hpp` — LevelDB/SQLite 分片进度
- [ ] `src/adapter/sftp/libssh2_pool.cpp` — SFTP 连接池、STOR
- [ ] `src/worker/transfer_engine.cpp` — mmap 读 + 分片上传
- [ ] `cmd/hi-file-worker/main.cpp`

### 2.2 断点续传

- [ ] 中断后重启 Worker，仅传未完成 Chunk
- [ ] `test/integration/resume_test.cpp`（本地 Mock SFTP）

**Week 2 验收**：100MB 文件中断后续传成功，MD5 一致。

---

## Week 3 · Coord + bench + Docker

### 3.1 Coordinator

- [ ] `cmd/hi-file-coord/main.cpp` — HTTP 提交 Task、gRPC 分配
- [ ] `src/coord/scheduler.cpp` — 单 Worker 租约

### 3.2 压测与容器

- [ ] `cmd/hi-file-bench/main.cpp` — 吞吐 MB/s、断点恢复耗时
- [ ] `deploy/docker/Dockerfile` + `docker-compose.yml`（coord + worker + mock-sftp）
- [ ] `bench/baseline-v1.json`

### 3.3 文档

- [ ] 更新 [hi-file-transfer-技术设计文档.md](hi-file-transfer-技术设计文档.md) 实现状态
- [ ] [简历-孙超](简历-孙超-v1.0.md) 增加 hi-file-transfer（实现完成后）

**M1 总验收**：

| 项 | 标准 |
|----|------|
| 1GB 文件断点续传 | 中断后续传成功，全局 MD5 一致 |
| Mock SFTP 集成 | Task 端到端 SUCCESS |
| 审计日志 | 含 customer_id、bank、file、md5、duration |
| Docker | `docker compose up` 跑通集成测试 |

---

## 与 hi-im-core 的依赖

| 依赖 | 说明 |
|------|------|
| **无代码依赖** | 不 link hi-im-core |
| **经验复用** | epoll、SPSC 队列、CMake/CTest/Docker 工程习惯 |
| **简历** | 与 hi-im-core 并列两个 C++ 项目 |

---

*清单版本：2026-07-01 · M1 完成后进入 MQ 接入与多 Worker（M2）。*
