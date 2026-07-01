cmake_minimum_required(VERSION 3.16)
project(hi-im-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
include(CTest)

# ---------------------------------------------------------------------------
# hiim_wire — bus wire v1 header + frame buffer
# ---------------------------------------------------------------------------
add_library(hiim_wire INTERFACE)
target_include_directories(hiim_wire INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# hiim_hub — Hub server (listener / reactor / worker / bridge)
# ---------------------------------------------------------------------------
add_library(hiim_hub STATIC
  src/hub/listener.cpp
  src/hub/reactor.cpp
  src/hub/distributor.cpp
  src/hub/worker.cpp
  src/hub/router.cpp
  src/hub/context_impl.cpp
  src/hub/bridge.cpp
  src/hub/hub_server.cpp
  src/hub/health_server.cpp
)
target_include_directories(hiim_hub PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(hiim_hub PUBLIC hiim_wire pthread)

# ---------------------------------------------------------------------------
# Binaries
# ---------------------------------------------------------------------------
add_executable(hi-im-hub cmd/hi-im-hub/main.cpp)
target_link_libraries(hi-im-hub PRIVATE hiim_hub)

add_executable(hi-im-bench cmd/hi-im-bench/main.cpp)
target_link_libraries(hi-im-bench PRIVATE hiim_wire pthread)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
function(hiim_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE hiim_wire hiim_hub pthread)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

hiim_add_test(wire_header_test test/wire_header_test.cpp)
hiim_add_test(frame_buffer_test test/frame_buffer_test.cpp)
hiim_add_test(router_test test/router_test.cpp)
hiim_add_test(hub_integration_test test/integration/hub_proxy_test.cpp)
