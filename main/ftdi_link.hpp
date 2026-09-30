#pragma once

#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "printer_link.hpp"
#include "usb/cdc_acm_host.h"

// PrinterLink over the Melzi's FT232R, with the S3 as USB host. Opens at 115200 8N1
// with DTR deasserted (asserting DTR resets the ATmega), and reopens after the device
// goes away.
class FtdiLink final : public PrinterLink {
 public:
  // Installs the USB host library and CDC-ACM driver and starts the connection task.
  esp_err_t Start();

  void SetReceiver(Receiver receiver) override { receiver_ = std::move(receiver); }
  esp_err_t Write(std::span<const uint8_t> data) override;
  esp_err_t Reset() override;

 private:
  static void UsbLibTask(void* arg);
  static void ConnectionTask(void* arg);
  static bool OnData(const uint8_t* data, size_t len, void* arg);
  static void OnEvent(const cdc_acm_host_dev_event_data_t* event, void* arg);

  void RunConnection();

  Receiver receiver_;
  TaskHandle_t connection_task_ = nullptr;
  std::mutex mutex_;                     // Guards device_.
  cdc_acm_dev_hdl_t device_ = nullptr;  // Null while disconnected.
};
