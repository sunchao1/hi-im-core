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


#include <array>
#include <cstring>
#include <iostream>
#include <vector>

#include "hiim/im/header.hpp"
#include "hiim/wire/header.hpp"
#include "wire/frame_buffer.hpp"

namespace {

using hiim::wire::EncodeFrame;
using hiim::wire::FrameBuffer;
using hiim::wire::HeaderFieldHost;
using hiim::wire::kFlagExp;
using hiim::wire::WireHeader;

#define EXPECT_TRUE(expr)                                                     \
  do {                                                                        \
    if (!(expr)) {                                                            \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #expr "\n";  \
      return 1;                                                               \
    }                                                                         \
  } while (0)

#define EXPECT_EQ(a, b)                                                       \
  do {                                                                        \
    if ((a) != (b)) {                                                         \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #a " != " #b \
                << " (" << (a) << " vs " << (b) << ")\n";                     \
      return 1;                                                               \
    }                                                                         \
  } while (0)

std::vector<uint8_t> MakeFrame(uint32_t type, uint32_t nid,
                               std::span<const uint8_t> payload) {
  return EncodeFrame(type, nid, kFlagExp, payload);
}

int TestSingleCompleteFrame() {
  const uint8_t payload[] = {'h', 'i'};
  FrameBuffer fb;
  const auto frame = MakeFrame(0x030Bu, 1, payload);
  fb.Append(frame);
  const auto view = fb.TryPopFrame();
  EXPECT_TRUE(view.has_value());
  EXPECT_EQ(HeaderFieldHost(view->header, &WireHeader::type), 0x030Bu);
  EXPECT_EQ(view->payload.size(), 2u);
  EXPECT_TRUE(fb.BufferedBytes() == 0);
  return 0;
}

int TestPartialFrameThenComplete() {
  const uint8_t payload[] = {1, 2, 3, 4, 5};
  const auto frame = MakeFrame(0x100u, 9, payload);
  FrameBuffer fb;

  fb.Append(std::span<const uint8_t>(frame.data(), 10));
  EXPECT_TRUE(!fb.TryPopFrame().has_value());

  fb.Append(std::span<const uint8_t>(frame.data() + 10, frame.size() - 10));
  const auto view = fb.TryPopFrame();
  EXPECT_TRUE(view.has_value());
  EXPECT_EQ(view->payload.size(), 5u);
  return 0;
}

int TestStickyMultipleFrames() {
  const auto f1 = MakeFrame(1, 1, std::span<const uint8_t>{});
  const uint8_t p2[] = {9};
  const auto f2 = MakeFrame(2, 2, p2);

  std::vector<uint8_t> blob;
  blob.insert(blob.end(), f1.begin(), f1.end());
  blob.insert(blob.end(), f2.begin(), f2.end());

  FrameBuffer fb;
  fb.Append(blob);

  const auto v1 = fb.TryPopFrame();
  const auto v2 = fb.TryPopFrame();
  EXPECT_TRUE(v1.has_value());
  EXPECT_TRUE(v2.has_value());
  EXPECT_EQ(HeaderFieldHost(v1->header, &WireHeader::type), 1u);
  EXPECT_EQ(HeaderFieldHost(v2->header, &WireHeader::type), 2u);
  EXPECT_TRUE(!fb.TryPopFrame().has_value());
  return 0;
}

int TestInvalidChecksumClearsBuffer() {
  std::vector<uint8_t> bad(20, 0xFF);
  FrameBuffer fb;
  fb.Append(bad);
  EXPECT_TRUE(!fb.TryPopFrame().has_value());
  EXPECT_EQ(fb.BufferedBytes(), 0u);
  return 0;
}

// Reactor pops all sticky frames before worker consumes; first FrameView must keep
// its payload after the second TryPopFrame mutates buf_ (old span impl read wrong nid).
int TestStickyDualFanoutPayloadSurvivesPop() {
  const uint8_t body[] = {'x'};
  const auto f1 = hiim::im::PackPayload(0x030Bu, 100001, 20001, 103, body);
  const auto f2 = hiim::im::PackPayload(0x030Bu, 100001, 20002, 103, body);
  const auto wire1 = MakeFrame(0x030Bu, 20001, f1);
  const auto wire2 = MakeFrame(0x030Bu, 20002, f2);

  std::vector<uint8_t> blob;
  blob.insert(blob.end(), wire1.begin(), wire1.end());
  blob.insert(blob.end(), wire2.begin(), wire2.end());

  FrameBuffer fb;
  fb.Append(blob);

  const auto v1 = fb.TryPopFrame();
  const auto v2 = fb.TryPopFrame();
  EXPECT_TRUE(v1.has_value());
  EXPECT_TRUE(v2.has_value());
  EXPECT_EQ(hiim::im::ReadDestNid(v1->payload), 20001u);
  EXPECT_EQ(hiim::im::ReadDestNid(v2->payload), 20002u);
  EXPECT_EQ(hiim::im::ReadSeq(v1->payload), 103u);
  EXPECT_EQ(hiim::im::ReadSeq(v2->payload), 103u);
  return 0;
}

}  // namespace

int main() {
  if (TestSingleCompleteFrame() != 0) return 1;
  if (TestPartialFrameThenComplete() != 0) return 1;
  if (TestStickyMultipleFrames() != 0) return 1;
  if (TestStickyDualFanoutPayloadSurvivesPop() != 0) return 1;
  if (TestInvalidChecksumClearsBuffer() != 0) return 1;
  std::cout << "frame_buffer_test: OK\n";
  return 0;
}
