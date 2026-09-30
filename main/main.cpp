#include "console_server.hpp"
#include "control_server.hpp"
#include "credentials.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "ftdi_link.hpp"
#include "nvs_flash.h"
#include "wifi_station.hpp"

namespace {

constexpr char kTag[] = "cr10-link";
constexpr uint16_t kConsolePort = 2323;

FtdiLink printer_link;
ConsoleServer console(printer_link, kConsolePort);
ControlServer control(printer_link);

void InitNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

// A new OTA image boots pending verification; if it is not marked valid before the next
// reset, the bootloader rolls back to the previous image.
void MarkImageValid() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    ESP_LOGI(kTag, "image marked valid");
  }
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(kTag, "%s starting", credentials::kHostname);
  InitNvs();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // The console sets the link's receiver, so it starts before the link.
  ESP_ERROR_CHECK(console.Start());
  ESP_ERROR_CHECK(printer_link.Start());
  ESP_ERROR_CHECK(control.Start());
  ESP_ERROR_CHECK(wifi_station::Start());

  wifi_station::WaitConnected();
  MarkImageValid();
}
