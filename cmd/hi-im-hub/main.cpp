// Copyright 2026 chao.sun
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// main.cpp — hi-im-hub 进程入口
//
// 职责：
//   解析命令行参数，启动双平面 Hub 服务，并在收到退出信号后优雅关闭。
//
// 在系统中的位置：
//   cmd 层可执行入口，封装 HubServer（src/hub/），对外暴露两个业务监听面：
//   - FORWARD 平面：gateway / proxy 接入（默认 :28888）
//   - BACKEND 平面：msgsvr 等后端接入（默认 :28889）
//   另含健康检查端口（默认 :8080）供运维探活。

#include <csignal>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "hiim/hub/hub_server.hpp"

namespace {

// 全局停止标志，由信号处理函数置位，主循环轮询后触发优雅退出。
std::atomic<bool> g_stop{false};

// SIGINT / SIGTERM 信号处理：仅设置停止标志，不做阻塞操作。
void OnSignal(int) { g_stop.store(true, std::memory_order_release); }

// 解析命令行参数，填充双平面 Hub 配置。
//
// 支持的启动参数：
//   --forward-listen ADDR   FORWARD 平面监听地址（默认 0.0.0.0:28888）
//   --backend-listen ADDR   BACKEND 平面监听地址（默认 0.0.0.0:28889）
//   --reactor-threads N     每平面 Reactor 线程数（默认 4，负责 epoll I/O）
//   --worker-threads N      每平面 Worker 线程数（默认 4，负责业务路由）
//   --auth-user USER        Proxy 认证用户名（默认 proxy）
//   --auth-pass PASS        Proxy 认证密码（默认 proxy）
//   --health-listen ADDR    健康检查 HTTP 监听地址（默认 0.0.0.0:8080）
//   --queue-capacity N      入站 MPSC 队列容量（默认 262144）
//   --help                  打印用法后退出
hiim::hub::DualHubConfig ParseArgs(int argc, char** argv) {
  hiim::hub::DualHubConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 < argc) {
        return argv[++i];
      }
      return {};
    };
    if (arg == "--forward-listen") {
      cfg.forward_listen = next();
    } else if (arg == "--backend-listen") {
      cfg.backend_listen = next();
    } else if (arg == "--reactor-threads") {
      cfg.reactor_threads = std::stoi(next());
    } else if (arg == "--worker-threads") {
      cfg.worker_threads = std::stoi(next());
    } else if (arg == "--auth-user") {
      cfg.auth_user = next();
    } else if (arg == "--auth-pass") {
      cfg.auth_pass = next();
    } else if (arg == "--health-listen") {
      cfg.health_listen = next();
    } else if (arg == "--queue-capacity") {
      cfg.queue_capacity = static_cast<std::size_t>(std::stoull(next()));
    } else if (arg == "--help") {
      std::cout << "Usage: hi-im-hub [options]\n"
                << "  --forward-listen ADDR   default 0.0.0.0:28888\n"
                << "  --backend-listen ADDR   default 0.0.0.0:28889\n"
                << "  --reactor-threads N     default 4\n"
                << "  --worker-threads N      default 4\n"
                << "  --auth-user USER        default proxy\n"
                << "  --auth-pass PASS        default proxy\n"
                << "  --health-listen ADDR    default 0.0.0.0:8080\n"
                << "  --queue-capacity N      default 262144\n";
      std::exit(0);
    }
  }
  return cfg;
}

}  // namespace

int main(int argc, char** argv) {
  // 1. 解析配置
  const auto cfg = ParseArgs(argc, argv);
  hiim::hub::HubServer server(cfg);

  // 2. 注册优雅退出信号
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  // 3. 启动双平面 Hub（FORWARD + BACKEND 各自 Reactor/Worker + 健康检查）
  if (!server.Start()) {
    std::cerr << "failed to start hi-im-hub\n";
    return 1;
  }

  std::cout << "hi-im-hub started\n"
            << "  FORWARD " << cfg.forward_listen << "\n"
            << "  BACKEND " << cfg.backend_listen << "\n"
            << "  HEALTH  " << cfg.health_listen << "\n";

  // 4. 主线程空转等待停止信号（实际 I/O 与路由在 HubServer 内部线程完成）
  while (!g_stop.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // 5. 优雅关闭：先 Stop 再 Wait 直至所有线程退出
  std::cout << "shutting down...\n";
  server.Stop();
  server.Wait();
  return 0;
}
