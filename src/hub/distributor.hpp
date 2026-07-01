#pragma once

#include <atomic>
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

class Distributor {
 public:
  explicit Distributor(HubContext& ctx);
  ~Distributor();

  Distributor(const Distributor&) = delete;
  Distributor& operator=(const Distributor&) = delete;

  void Start();
  void Stop();
  void Join();

 private:
  void Run();

  HubContext& ctx_;
  std::thread thread_;
#if defined(__linux__)
  int epfd_{-1};
#else
  int kqfd_{-1};
#endif
};

}  // namespace hiim::hub
