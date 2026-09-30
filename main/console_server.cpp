#include "console_server.hpp"

#include <algorithm>
#include <array>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace {

constexpr char kTag[] = "console";
constexpr char kBusy[] = "cr10-link: console in use\r\n";

// A client that vanished without closing is dropped after about 10 + 3 * 5 s.
constexpr int kKeepIdleS = 10;
constexpr int kKeepIntervalS = 5;
constexpr int kKeepCount = 3;

constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 5;

}  // namespace

esp_err_t ConsoleServer::Start() {
  listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_fd_ < 0) return ESP_FAIL;
  const int reuse = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(listen_fd_, 1) != 0) {
    ESP_LOGE(kTag, "bind/listen on %u: errno %d", port_, errno);
    close(listen_fd_);
    listen_fd_ = -1;
    return ESP_FAIL;
  }
  link_.SetReceiver([this](std::span<const uint8_t> data) { OnPrinterBytes(data); });
  if (xTaskCreate(Task, "console", kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "listening on port %u", port_);
  return ESP_OK;
}

void ConsoleServer::Task(void* arg) { static_cast<ConsoleServer*>(arg)->Run(); }

void ConsoleServer::Run() {
  while (true) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listen_fd_, &readable);
    if (client_fd_ >= 0) FD_SET(client_fd_, &readable);
    const int max_fd = std::max(listen_fd_, client_fd_);
    if (select(max_fd + 1, &readable, nullptr, nullptr, nullptr) < 0) {
      ESP_LOGW(kTag, "select: errno %d", errno);
      continue;
    }
    if (FD_ISSET(listen_fd_, &readable)) Accept();
    if (client_fd_ >= 0 && FD_ISSET(client_fd_, &readable)) ServeClient();
  }
}

void ConsoleServer::Accept() {
  const int fd = accept(listen_fd_, nullptr, nullptr);
  if (fd < 0) return;
  if (client_fd_ >= 0) {
    send(fd, kBusy, sizeof(kBusy) - 1, MSG_DONTWAIT);
    close(fd);
    return;
  }
  const int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &kKeepIdleS, sizeof(kKeepIdleS));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &kKeepIntervalS, sizeof(kKeepIntervalS));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &kKeepCount, sizeof(kKeepCount));
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  std::lock_guard lock(mutex_);
  client_fd_ = fd;
  ESP_LOGI(kTag, "client connected");
}

void ConsoleServer::ServeClient() {
  std::array<uint8_t, 256> buf;
  const int n = recv(client_fd_, buf.data(), buf.size(), 0);
  if (n <= 0) {
    CloseClient();
    return;
  }
  if (const esp_err_t err = link_.Write(std::span(buf.data(), n)); err != ESP_OK) {
    ESP_LOGD(kTag, "dropped %d bytes to printer: %s", n, esp_err_to_name(err));
  }
}

void ConsoleServer::CloseClient() {
  std::lock_guard lock(mutex_);
  close(client_fd_);
  client_fd_ = -1;
  ESP_LOGI(kTag, "client disconnected");
}

void ConsoleServer::OnPrinterBytes(std::span<const uint8_t> data) {
  std::lock_guard lock(mutex_);
  if (client_fd_ < 0) return;
  send(client_fd_, data.data(), data.size(), MSG_DONTWAIT);
}
