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


#include <iostream>

#include "hiim/hub/router.hpp"

namespace {

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

using hiim::hub::Router;
using hiim::hub::Subscriber;

int TestSubscribeAndPublishExpand() {
  Router router;
  Subscriber s1{1, 10, 20001, 0};
  Subscriber s2{1, 11, 20002, 1};
  EXPECT_TRUE(router.Subscribe(0x030B, s1).ok());
  EXPECT_TRUE(router.Subscribe(0x030B, s2).ok());
  EXPECT_EQ(router.SubscriptionCount(0x030B), 2u);

  const auto subs = router.FindSubscribers(0x030B);
  EXPECT_EQ(subs.size(), 2u);
  return 0;
}

int TestNidRoute() {
  Router router;
  router.BindNid(20001, 10, 0);
  const auto route = router.FindNidRoute(20001);
  EXPECT_TRUE(route.has_value());
  EXPECT_EQ(route->sid, 10u);
  EXPECT_EQ(route->reactor_idx, 0);

  router.UnbindNid(20001, 10);
  EXPECT_TRUE(!router.FindNidRoute(20001).has_value());
  return 0;
}

int TestRemoveSession() {
  Router router;
  Subscriber s1{1, 10, 20001, 0};
  router.Subscribe(0x030B, s1);
  router.BindNid(20001, 10, 0);
  router.RemoveSession(10, 20001);
  EXPECT_EQ(router.SubscriptionCount(0x030B), 0u);
  EXPECT_TRUE(!router.FindNidRoute(20001).has_value());
  return 0;
}

}  // namespace

int main() {
  if (TestSubscribeAndPublishExpand() != 0) return 1;
  if (TestNidRoute() != 0) return 1;
  if (TestRemoveSession() != 0) return 1;
  std::cout << "router_test: OK\n";
  return 0;
}
