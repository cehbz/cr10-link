#include "ftdi_link.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/vcp_ftdi.h"

namespace {

constexpr char kTag[] = "ftdi_link";

constexpr uint32_t kOpenTimeoutMs = 1000;
constexpr size_t kOutBufferSize = 256;
constexpr uint32_t kWriteTimeoutMs = 1000;
// DTR# reaches RESET through 100 nF, so the reset pulse width is set by that RC, not by
// how long DTR is held; this only has to outlast it.
constexpr TickType_t kResetHold = pdMS_TO_TICKS(100);

constexpr UBaseType_t kUsbLibPriority = 20;
constexpr UBaseType_t kConnectionPriority = 5;
constexpr uint32_t kTaskStack = 4096;

}  // namespace

esp_err_t FtdiLink::Start() {
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .root_port_unpowered = false,
      .intr_flags = ESP_INTR_FLAG_LOWMED,
      .enum_filter_cb = nullptr,
      .fifo_settings_custom = {},
      .peripheral_map = 0,
  };
  ESP_RETURN_ON_ERROR(usb_host_install(&host_config), kTag, "usb_host_install");
  if (xTaskCreate(UsbLibTask, "usb_lib", kTaskStack, nullptr, kUsbLibPriority, nullptr) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  ESP_RETURN_ON_ERROR(cdc_acm_host_install(nullptr), kTag, "cdc_acm_host_install");
  if (xTaskCreate(ConnectionTask, "ftdi_conn", kTaskStack, this, kConnectionPriority,
                  &connection_task_) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t FtdiLink::Write(std::span<const uint8_t> data) {
  if (data.empty()) return ESP_OK;
  std::lock_guard lock(mutex_);
  if (device_ == nullptr) return ESP_ERR_INVALID_STATE;
  return cdc_acm_host_data_tx_blocking(device_, data.data(), data.size(), kWriteTimeoutMs);
}

esp_err_t FtdiLink::Reset() {
  std::lock_guard lock(mutex_);
  if (device_ == nullptr) return ESP_ERR_INVALID_STATE;
  ESP_LOGI(kTag, "resetting printer: DTR pulse");
  ESP_RETURN_ON_ERROR(cdc_acm_host_set_control_line_state(device_, true, false), kTag,
                      "assert DTR");
  vTaskDelay(kResetHold);
  return cdc_acm_host_set_control_line_state(device_, false, false);
}

void FtdiLink::UsbLibTask(void*) {
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

void FtdiLink::ConnectionTask(void* arg) { static_cast<FtdiLink*>(arg)->RunConnection(); }

void FtdiLink::RunConnection() {
  const cdc_acm_host_device_config_t config = {
      .connection_timeout_ms = kOpenTimeoutMs,
      .out_buffer_size = kOutBufferSize,
      .in_buffer_size = 0,
      .event_cb = OnEvent,
      .data_cb = OnData,
      .user_arg = this,
  };
  while (true) {
    cdc_acm_dev_hdl_t device = nullptr;
    // Sends only the FTDI reset and 115200 8N1 line coding; DTR and RTS are untouched.
    const esp_err_t err = ftdi_vcp_open(VCP_FTDI_PID_FT232, 0, &config, &device);
    if (err == ESP_ERR_NOT_FOUND) continue;
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "open failed: %s", esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(kOpenTimeoutMs));
      continue;
    }
    // Deasserting is a rising edge on DTR#, which the RESET coupling capacitor does not
    // pass as a reset. It leaves DTR in a known state so that Reset() produces a falling edge.
    if (const esp_err_t e = cdc_acm_host_set_control_line_state(device, false, false); e != ESP_OK) {
      ESP_LOGW(kTag, "deassert DTR/RTS: %s", esp_err_to_name(e));
    }
    {
      std::lock_guard lock(mutex_);
      device_ = device;
    }
    ESP_LOGI(kTag, "printer connected");

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Given by OnEvent on disconnect.

    {
      std::lock_guard lock(mutex_);
      cdc_acm_host_close(device_);
      device_ = nullptr;
    }
    ESP_LOGI(kTag, "printer disconnected");
  }
}

bool FtdiLink::OnData(const uint8_t* data, size_t len, void* arg) {
  auto* self = static_cast<FtdiLink*>(arg);
  if (self->receiver_) self->receiver_(std::span<const uint8_t>(data, len));
  return true;
}

void FtdiLink::OnEvent(const cdc_acm_host_dev_event_data_t* event, void* arg) {
  auto* self = static_cast<FtdiLink*>(arg);
  switch (event->type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
      xTaskNotifyGive(self->connection_task_);
      break;
    case CDC_ACM_HOST_ERROR:
      ESP_LOGW(kTag, "CDC-ACM error %d", event->data.error);
      break;
    default:
      break;
  }
}
