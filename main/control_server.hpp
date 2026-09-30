#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "printer_link.hpp"

// HTTP server on port 80:
//   POST /ota    body is an app image; written to the passive OTA slot, then reboot.
//   POST /reset  pulses DTR to reset the printer.
class ControlServer {
 public:
  explicit ControlServer(PrinterLink& link) : link_(link) {}

  esp_err_t Start();

 private:
  static esp_err_t HandleOta(httpd_req_t* req);
  static esp_err_t HandleReset(httpd_req_t* req);

  PrinterLink& link_;
  httpd_handle_t server_ = nullptr;
};
