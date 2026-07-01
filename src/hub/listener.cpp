#include "hub/listener.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <atomic>

namespace hiim::hub {

namespace {

int SetCloseOnExec(int fd) {
#if defined(FD_CLOEXEC)
  return fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
  (void)fd;
  return 0;
#endif
}

int CreateTcpSocket() {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    SetCloseOnExec(fd);
  }
  return fd;
}
int SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int PickReactor(int reactor_count) {
  static std::atomic<uint64_t> round{0};
  const auto n = round.fetch_add(1, std::memory_order_relaxed);
  return static_cast<int>(n % static_cast<uint64_t>(reactor_count));
}

}  // namespace

Listener::Listener(HubContext& ctx, int reactor_count)
    : ctx_(ctx), reactor_count_(std::max(1, reactor_count)) {}

Listener::~Listener() { Stop(); }

bool Listener::ParseListenAddr(int& port) const {
  const auto& addr = ctx_.Config().listen_addr;
  const auto colon = addr.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  port = std::stoi(addr.substr(colon + 1));
  return true;
}

bool Listener::Start() {
  if (started_.exchange(true)) {
    return true;
  }

  int port = 0;
  if (!ParseListenAddr(port)) {
    std::cerr << "[listener] invalid listen addr: " << ctx_.Config().listen_addr << "\n";
    started_.store(false);
    return false;
  }

  listen_fd_ = CreateTcpSocket();
  if (listen_fd_ < 0) {
    std::cerr << "[listener] socket failed\n";
    started_.store(false);
    return false;
  }

  int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
    std::cerr << "[listener] bind failed on port " << port << "\n";
    close(listen_fd_);
    listen_fd_ = -1;
    started_.store(false);
    return false;
  }
  if (listen(listen_fd_, 512) < 0) {
    std::cerr << "[listener] listen failed\n";
    close(listen_fd_);
    listen_fd_ = -1;
    started_.store(false);
    return false;
  }

  ctx_.MarkListening(true);
  thread_ = std::thread([this] { Run(); });
  return true;
}

void Listener::Stop() {
  ctx_.RequestStop();
  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

void Listener::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Listener::Run() {
  while (ctx_.Running().load(std::memory_order_acquire)) {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    const int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client), &len);
    if (fd < 0) {
      if (!ctx_.Running().load(std::memory_order_acquire)) {
        break;
      }
      continue;
    }
    if (SetNonBlocking(fd) < 0) {
      close(fd);
      continue;
    }

    const int reactor_idx = PickReactor(reactor_count_);
    NewConnection conn{};
    conn.fd = fd;
    conn.sid = ctx_.NextSid();

    auto& q = ctx_.ConnQueue(reactor_idx);
    if (!q.Push(std::move(conn))) {
      std::cerr << "[listener] conn queue full, dropping fd\n";
      close(fd);
      continue;
    }
    ctx_.ReactorWakeup(reactor_idx).Notify();
  }
}

}  // namespace hiim::hub
