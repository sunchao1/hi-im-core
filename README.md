# hi-im-core

hi-im 是一款面向高并发场景的即时通信系统；**hi-im-core** 为其 C++ 进程消息总线（Hub），净室重写必嗨 RTMQ + frwder 转发平面。

**许可证**：[Apache License 2.0](LICENSE)

## 文档

| 文档 | 说明 |
|------|------|
| [doc/技术设计文档.md](doc/技术设计文档.md) | 主设计：架构、线程模型、API、分片、可观测 |
| [doc/协议规范-bus-wire-v1.md](doc/协议规范-bus-wire-v1.md) | 20 字节线协议（与 RTMQ 兼容） |
| [doc/M1-实施清单.md](doc/M1-实施清单.md) | 第一阶段开发任务与验收 |

## 产物（规划）

| 二进制 | 说明 |
|--------|------|
| `hi-im-hub` | Hub 主进程（FORWARD 28888 / BACKEND 28889） |
| `hi-im-bench` | 压测，对齐必嗨 rtmq-bench |

## 生态

- **hi-im-proxy** — Go Proxy 客户端（独立仓库）
- **hi-im-gateway / msgsvr / …** — Go 业务服务

全栈方案见 beehive-im 参考仓库 `doc/hi-im-档C技术方案设计.md`。

## 状态

M1 开发中 · wire 单测、Hub 双平面、bridge、bench 已实现。

### 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 运行 Hub

```bash
./build/hi-im-hub \
  --forward-listen 0.0.0.0:28888 \
  --backend-listen 0.0.0.0:28889 \
  --health-listen 0.0.0.0:8080
```

健康检查：`GET /healthz`、`GET /readyz`（默认 `:8080`）。

### 压测

```bash
# 先启动 hi-im-hub，再：
./build/hi-im-bench -mode publish -duration 10s -payload 256
./build/hi-im-bench -mode unicast -duration 10s -payload 256
```

M1 验收目标：Linux 单机 publish 模式 **recv/s ≥ 140000**（256B  payload）。基线见 [bench/baseline-v1.json](bench/baseline-v1.json)。
