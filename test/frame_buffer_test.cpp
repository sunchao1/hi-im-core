#include <array>
#include <cstring>
#include <iostream>
#include <vector>

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

}  // namespace

int main() {
  if (TestSingleCompleteFrame() != 0) return 1;
  if (TestPartialFrameThenComplete() != 0) return 1;
  if (TestStickyMultipleFrames() != 0) return 1;
  if (TestInvalidChecksumClearsBuffer() != 0) return 1;
  std::cout << "frame_buffer_test: OK\n";
  return 0;
}
