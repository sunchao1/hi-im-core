#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

class Listener {
 public:
  Listener(HubContext& ctx, int reactor_count);
  ~Listener();

  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  bool Start();
  void Stop();
  void Join();

 private:
  void Run();
  bool ParseListenAddr(int& port) const;

  HubContext& ctx_;
  int reactor_count_{1};
  int listen_fd_{-1};
  std::thread thread_;
  std::atomic<bool> started_{false};
};

}  // namespace hiim::hub
