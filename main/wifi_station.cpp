#include "wifi_station.hpp"

#include <cstring>

#include "credentials.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace wifi_station {
namespace {

constexpr char kTag[] = "wifi";
constexpr EventBits_t kConnectedBit = BIT0;

EventGroupHandle_t events = nullptr;
esp_netif_t* netif = nullptr;

// Applied on each association: the address is only announced (IP_EVENT_STA_GOT_IP)
// when it is set on a netif that is up.
void ApplyStaticIp() {
  esp_netif_dhcpc_stop(netif);
  esp_netif_ip_info_t ip = {};
  esp_netif_str_to_ip4(credentials::kIpAddress, &ip.ip);
  esp_netif_str_to_ip4(credentials::kNetmask, &ip.netmask);
  esp_netif_str_to_ip4(credentials::kGateway, &ip.gw);
  if (const esp_err_t err = esp_netif_set_ip_info(netif, &ip); err != ESP_OK) {
    ESP_LOGE(kTag, "set static IP: %s", esp_err_to_name(err));
  }
}

void OnWifiEvent(void*, esp_event_base_t, int32_t id, void*) {
  switch (id) {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_CONNECTED:
      ApplyStaticIp();
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      xEventGroupClearBits(events, kConnectedBit);
      esp_wifi_connect();
      break;
    default:
      break;
  }
}

void OnGotIp(void*, esp_event_base_t, int32_t, void*) {
  ESP_LOGI(kTag, "up at %s", credentials::kIpAddress);
  xEventGroupSetBits(events, kConnectedBit);
}

}  // namespace

esp_err_t Start() {
  events = xEventGroupCreate();
  netif = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(netif, credentials::kHostname);

  const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), kTag, "esp_wifi_init");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, OnWifiEvent, nullptr), kTag,
      "register WIFI_EVENT");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, OnGotIp, nullptr), kTag,
      "register IP_EVENT");

  wifi_config_t config = {};
  std::strncpy(reinterpret_cast<char*>(config.sta.ssid), credentials::kWifiSsid,
               sizeof(config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(config.sta.password), credentials::kWifiPassword,
               sizeof(config.sta.password));
  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), kTag, "set config");
  return esp_wifi_start();
}

void WaitConnected() {
  xEventGroupWaitBits(events, kConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
}

}  // namespace wifi_station
