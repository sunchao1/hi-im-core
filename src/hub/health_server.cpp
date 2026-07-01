#include "hub/health_server.hpp"

#include "hiim/hub/hub_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>

namespace hiim::hub {

HealthServer::HealthServer(std::string listen_addr, HubServer* hub)
    : listen_addr_(std::move(listen_addr)), hub_(hub) {}

HealthServer::~HealthServer() { Stop(); }

bool HealthServer::ParsePort(int& port) const {
  const auto colon = listen_addr_.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  port = std::stoi(listen_addr_.substr(colon + 1));
  return true;
}

bool HealthServer::Start() {
  int port = 0;
  if (!ParsePort(port)) {
    return false;
  }
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    return false;
  }
  int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (listen(listen_fd_, 32) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  running_.store(true, std::memory_order_release);
  thread_ = std::thread([this] { Run(); });
  return true;
}

void HealthServer::Stop() {
  running_.store(false, std::memory_order_release);
  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

void HealthServer::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void HealthServer::Run() {
  while (running_.load(std::memory_order_acquire)) {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    const int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client), &len);
    if (fd < 0) {
      continue;
    }

    char req[1024];
    const ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
      close(fd);
      continue;
    }
    req[n] = '\0';

    const bool ready_path = std::strstr(req, "/readyz") != nullptr;
    const bool ok = ready_path ? (hub_ != nullptr && hub_->IsReady()) : true;
    const char* body = ok ? "ok\n" : "not ready\n";
    const char* status = ok ? "200 OK" : "503 Service Unavailable";

    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: text/plain\r\n"
        << "Content-Length: " << std::strlen(body) << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    const auto resp = oss.str();
    (void)send(fd, resp.data(), resp.size(), 0);
    close(fd);
  }
}

}  // namespace hiim::hub
