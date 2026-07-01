#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "hiim/wire/header.hpp"

namespace {

using hiim::wire::DecodeHeader;
using hiim::wire::EncodeFrame;
using hiim::wire::EncodeHeader;
using hiim::wire::HeaderFieldHost;
using hiim::wire::HostToBe32;
using hiim::wire::kFlagExp;
using hiim::wire::kWireChecksum;
using hiim::wire::kWireHeaderSize;
using hiim::wire::MakeHeader;
using hiim::wire::ValidateChecksum;
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

int TestSizeMatchesRtmqLayout() {
  EXPECT_EQ(sizeof(WireHeader), 20u);
  EXPECT_EQ(kWireHeaderSize, 20u);
  return 0;
}

int TestEndianRoundTrip() {
  const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
  const auto frame = EncodeFrame(0x030Bu, 20001u, kFlagExp, payload);

  WireHeader decoded{};
  EXPECT_TRUE(DecodeHeader(frame, decoded));
  EXPECT_EQ(HeaderFieldHost(decoded, &WireHeader::type), 0x030Bu);
  EXPECT_EQ(HeaderFieldHost(decoded, &WireHeader::nid), 20001u);
  EXPECT_EQ(HeaderFieldHost(decoded, &WireHeader::flag), kFlagExp);
  EXPECT_EQ(HeaderFieldHost(decoded, &WireHeader::length), 4u);
  EXPECT_EQ(HeaderFieldHost(decoded, &WireHeader::chksum), kWireChecksum);
  return 0;
}

int TestRejectsInvalidChecksum() {
  WireHeader h = MakeHeader(1, 2, 0, 0);
  h.chksum = HostToBe32(0xDEADBEEFu);

  std::array<uint8_t, kWireHeaderSize> bytes{};
  EncodeHeader(h, bytes);
  WireHeader out{};
  EXPECT_TRUE(!DecodeHeader(bytes, out));
  EXPECT_TRUE(!ValidateChecksum(h));
  return 0;
}

int TestFieldOrderMatchesPackedLayout() {
  WireHeader h{};
  h.type = HostToBe32(0x11111111u);
  h.nid = HostToBe32(0x22222222u);
  h.flag = HostToBe32(0x33333333u);
  h.length = HostToBe32(0x44444444u);
  h.chksum = HostToBe32(kWireChecksum);

  std::array<uint8_t, kWireHeaderSize> bytes{};
  EncodeHeader(h, bytes);

  EXPECT_EQ(bytes[0], 0x11);
  EXPECT_EQ(bytes[4], 0x22);
  EXPECT_EQ(bytes[8], 0x33);
  EXPECT_EQ(bytes[12], 0x44);
  EXPECT_EQ(bytes[16], 0x1F);
  EXPECT_EQ(bytes[17], 0xE2);
  return 0;
}

}  // namespace

int main() {
  if (TestSizeMatchesRtmqLayout() != 0) return 1;
  if (TestEndianRoundTrip() != 0) return 1;
  if (TestRejectsInvalidChecksum() != 0) return 1;
  if (TestFieldOrderMatchesPackedLayout() != 0) return 1;
  std::cout << "wire_header_test: OK\n";
  return 0;
}
