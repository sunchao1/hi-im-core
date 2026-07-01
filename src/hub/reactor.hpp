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


#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>

#include "hiim/hub/context.hpp"
#include "wire/frame_buffer.hpp"

namespace hiim::hub {

class Reactor {
 public:
  Reactor(HubContext& ctx, int idx, int worker_count);
  ~Reactor();

  Reactor(const Reactor&) = delete;
  Reactor& operator=(const Reactor&) = delete;

  void Start();
  void Stop();
  void Join();

 private:
  struct Session {
    uint64_t sid{0};
    int fd{-1};
    bool authed{false};
    uint32_t gid{0};
    uint32_t nid{0};
    hiim::wire::FrameBuffer fb;
    std::vector<uint8_t> outbuf;
  };

  void Run();
  void DrainNewConnections();
  void DrainSendQueue();
  void HandleReadable(int fd);
  void HandleWritable(int fd);
  void CloseSession(uint64_t sid);
  void EnqueueInbound(Session& session, const hiim::wire::FrameView& frame);
  bool HandleSystem(Session& session, const hiim::wire::FrameView& frame);
  bool SendBytes(Session& session, std::span<const uint8_t> data);
  void UpdateInterest(Session& session, bool want_read, bool want_write);
  int PickWorker(uint64_t sid) const;

  HubContext& ctx_;
  int idx_;
  int worker_count_;
  std::thread thread_;
  std::unordered_map<int, uint64_t> fd_to_sid_;
  std::unordered_map<uint64_t, Session> sessions_;
#if defined(__linux__)
  int epfd_{-1};
#else
  int kqfd_{-1};
#endif
};

}  // namespace hiim::hub
