#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace hiim::hub {

class HubServer;

class HealthServer {
 public:
  HealthServer(std::string listen_addr, HubServer* hub);
  ~HealthServer();

  bool Start();
  void Stop();
  void Join();

 private:
  void Run();
  bool ParsePort(int& port) const;

  std::string listen_addr_;
  HubServer* hub_{nullptr};
  int listen_fd_{-1};
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace hiim::hub
