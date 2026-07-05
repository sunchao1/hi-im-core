# hi-im-core 文档

> **hi-im-core** 是 [hi-im](https://github.com/) 生态中的 C++ 进程消息总线（Hub），净室重写必嗨 RTMQ + frwder 转发平面。  
> **许可证**：[Apache License 2.0](../LICENSE)

---

## 阅读顺序

| 顺序 | 文档 | 内容 |
|------|------|------|
| 1 | [技术设计文档.md](技术设计文档.md) | **主文档**：背景、架构、协议、线程模型、API、分片、可观测、目录、里程碑 |
| 2 | [协议规范-bus-wire-v1.md](协议规范-bus-wire-v1.md) | 20 字节线协议、系统 cmd、与 RTMQ 兼容对照 |
| 3 | [M1-实施清单.md](M1-实施清单.md) | 第一阶段（单 Shard + bench）文件级任务与验收 |
| 4 | [theme/README.md](theme/README.md) | **面试专题**：epoll、SPSC/MPSC、Bug2、双平面、async_send、群聊端到端（含流程图） |

---

## 生态位置

```text
hi-im-core（本仓库）     ← C++ Hub + bridge + C++ Proxy SDK + bench
hi-im-proxy（独立仓库）  ← Go Proxy，纯 Go 实现 bus wire v1
hi-im-gateway / msgsvr … ← Go 业务，通过 hi-im-proxy 连 Hub
```

全栈方案见 beehive-im 仓库中的 `doc/hi-im-档C技术方案设计.md`（生态总览）；**本目录只描述 hi-im-core 边界内设计**。

---

## 二进制与配置

| 产物 | 说明 |
|------|------|
| `hi-im-hub` | Hub 主进程（双平面 FORWARD/BACKEND + bridge） |
| `hi-im-bench` | 压测工具，对齐必嗨 `rtmq-bench` |

默认端口（单 Shard）：**28888** FORWARD、**28889** BACKEND（与必嗨 frwder 一致，便于对照迁移）。
