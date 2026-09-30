#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "esp_err.h"

// Byte-level link to the printer's serial port.
class PrinterLink {
 public:
  // Called with bytes from the printer, in the link's driver task. Must not block.
  using Receiver = std::function<void(std::span<const uint8_t>)>;

  virtual ~PrinterLink() = default;

  // Set once, before the link starts.
  virtual void SetReceiver(Receiver receiver) = 0;

  // ESP_ERR_INVALID_STATE while the printer is not connected.
  virtual esp_err_t Write(std::span<const uint8_t> data) = 0;

  // Resets the printer's MCU. ESP_ERR_INVALID_STATE while the printer is not connected.
  virtual esp_err_t Reset() = 0;
};
