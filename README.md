# hi-im-core

hi-im 是一款面向高并发场景的即时通信系统；**hi-im-core** 为其 C++ 进程消息总线（Hub），净室重写必嗨 RTMQ + frwder 转发平面。

**许可证**：[Apache License 2.0](LICENSE) · 详见 [NOTICE](NOTICE)

**版权**：Copyright 2026 chao.sun

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

M1 已完成：wire 单测、Hub 双平面、bridge、Docker 验收、**publish 压测达标**（recv/s ≥ 140000）。

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

M1 验收口径：`publish` 模式、**10s**、**256B** payload、**recv/s ≥ 140000**（Linux 单机；见 [doc/M1-实施清单.md](doc/M1-实施清单.md)）。

#### M1 压测结果（2026-07-01）

| 项 | 值 |
|----|-----|
| 模式 | `publish` |
| 时长 | 10s（含 2s 预热） |
| Payload | 256 B |
| Publisher 数 | **1** |
| Hub 线程 | 4 reactor + 4 worker |
| 队列容量 | 262144 |
| **recv/s** | **153,942** ✅ |
| sent/s | 184,936 |
| 10s 总 recv | 1,539,429 |
| 10s 总 sent | 1,849,368 |
| M1 目标 | recv/s ≥ 140,000 |
| 结论 | **PASS** |

运行环境：Docker Compose（`docker-compose.load.yml`），Hub 镜像 linux/arm64，宿主机 macOS Docker Desktop。  
机器可读基线：[bench/baseline-v1.json](bench/baseline-v1.json)。

> **注意**：M1 publish 压测请用 **1 个 publisher**（`docker-compose.load.yml` 已配置）。多 publisher 会在 FORWARD 侧争用连接，recv/s 会显著偏低，不能代表 Hub 极限吞吐。连续跑分会有波动（约 98k～154k），建议 Hub 稳定运行数秒后再 bench。

**Docker Compose（推荐，与基线一致）**

```bash
docker compose -f docker-compose.yml -f docker-compose.load.yml --profile run up -d hub
docker compose -f docker-compose.yml -f docker-compose.load.yml --profile bench run --rm bench
```

期望输出含：

```text
mode=publish duration=10s payload=256B publishers=1 ...
sent/s=... recv/s=...
m1_target_recv_per_sec=140000 PASS
```

**本机二进制（Hub 已启动后）**

```bash
./build/hi-im-bench -mode publish -duration 10s -payload 256 -publishers 1 \
  -forward 127.0.0.1:28888 -backend 127.0.0.1:28889
./build/hi-im-bench -mode unicast -duration 10s -payload 256
```

### Docker Compose 验收

```bash
# 编译 + ctest
docker compose --profile build run --rm --build builder

# 启动 Hub
docker compose --profile run up -d hub

# 健康检查 + bridge 端到端冒烟
docker compose --profile accept run --rm accept

# 一键（构建 → 启动 → 验收，失败非 0 退出）
docker compose --profile build run --rm --build builder && \
docker compose --profile run --profile accept up --abort-on-container-exit --exit-code-from accept

# 停止
docker compose --profile run down
```

端口（Docker Compose 宿主机映射，默认）：

| 服务 | 宿主机 | 容器内 | 说明 |
|------|--------|--------|------|
| FORWARD | 28888 | 28888 | Proxy 上行 |
| BACKEND | 28889 | 28889 | 业务下行 |
| health | **18080** | 8080 | `/healthz`、`/readyz` |

health 默认映射到 **18080**，避免与本机常见 8080 服务冲突。可在 `.env` 设置 `HIIM_HEALTH_HOST_PORT=8080`（或其他空闲端口）。

#### Docker Hub 拉取超时

报错含 `auth.docker.io` / `i/o timeout` 时，是访问 Docker Hub 失败（国内常见），**不是** compose 配置错误。

**方式 A（推荐）**：使用项目内置镜像源变量

```bash
cp .env.example .env
docker compose --profile build run --rm --build builder
```

**方式 B**：叠加国内 compose 覆盖文件

```bash
docker compose -f docker-compose.yml -f docker-compose.mirror.cn.yml \
  --profile build run --rm --build builder
```

**方式 C**：配置 Docker 守护进程镜像加速（Docker Desktop → Settings → Docker Engine）：

```json
{
  "registry-mirrors": [
    "https://docker.m.daocloud.io"
  ]
}
```

保存后重启 Docker，仍可使用默认 `ubuntu:22.04`。

#### `CMakeCache.txt` 路径冲突

若构建 hub 镜像时报 `CMakeCache.txt directory ... is different than ... /workspace/build`，是因为本机 `build/` 被 `COPY` 进镜像。项目已提供 `.dockerignore` 排除 `build/`；仍报错时可先 `rm -rf build` 再构建 hub 镜像。

#### 端口被占用（`bind: address already in use`）

本机已有进程占用 8080 时，Compose 默认将 health 映射到 **18080**。确认 `.env` 中：

```bash
HIIM_HEALTH_HOST_PORT=18080
```

或改为其他空闲端口，然后 `docker compose --profile run up -d hub`。容器内 `accept` 仍访问 `hub:8080`，无需改动。
