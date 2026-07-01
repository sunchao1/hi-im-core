#pragma once

#include <atomic>
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

class Worker {
 public:
  Worker(HubContext& ctx, int idx);
  ~Worker();

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void Start();
  void Stop();
  void Join();

 private:
  void Run();

  HubContext& ctx_;
  int idx_;
  std::thread thread_;
#if defined(__linux__)
  int epfd_{-1};
#else
  int kqfd_{-1};
#endif
};

}  // namespace hiim::hub
