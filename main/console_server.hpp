#pragma once

#include <cstdint>
#include <mutex>
#include <span>

#include "esp_err.h"
#include "printer_link.hpp"

// Raw TCP console bridging bytes both ways between one client and the printer link.
// A second client is told the console is busy and disconnected. Printer output with no
// client connected, or that the client's socket cannot take, is dropped.
class ConsoleServer {
 public:
  ConsoleServer(PrinterLink& link, uint16_t port) : link_(link), port_(port) {}

  // Binds and listens, sets the link's receiver and starts the server task.
  esp_err_t Start();

 private:
  static void Task(void* arg);
  void Run();
  void Accept();
  void ServeClient();
  void CloseClient();
  void OnPrinterBytes(std::span<const uint8_t> data);

  PrinterLink& link_;
  const uint16_t port_;
  int listen_fd_ = -1;
  std::mutex mutex_;    // Guards client_fd_ against the link's driver task.
  int client_fd_ = -1;  // Written only by the server task.
};
