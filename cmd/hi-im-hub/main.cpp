#include <csignal>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "hiim/hub/hub_server.hpp"

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true, std::memory_order_release); }

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
    } else if (arg == "--help") {
      std::cout << "Usage: hi-im-hub [options]\n"
                << "  --forward-listen ADDR   default 0.0.0.0:28888\n"
                << "  --backend-listen ADDR   default 0.0.0.0:28889\n"
                << "  --reactor-threads N     default 4\n"
                << "  --worker-threads N      default 4\n"
                << "  --auth-user USER        default proxy\n"
                << "  --auth-pass PASS        default proxy\n"
                << "  --health-listen ADDR    default 0.0.0.0:8080\n";
      std::exit(0);
    }
  }
  return cfg;
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = ParseArgs(argc, argv);
  hiim::hub::HubServer server(cfg);

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  if (!server.Start()) {
    std::cerr << "failed to start hi-im-hub\n";
    return 1;
  }

  std::cout << "hi-im-hub started\n"
            << "  FORWARD " << cfg.forward_listen << "\n"
            << "  BACKEND " << cfg.backend_listen << "\n"
            << "  HEALTH  " << cfg.health_listen << "\n";

  while (!g_stop.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  std::cout << "shutting down...\n";
  server.Stop();
  server.Wait();
  return 0;
}
