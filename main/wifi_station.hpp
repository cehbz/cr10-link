#pragma once

#include "esp_err.h"

// WiFi station with the fixed address and hostname from credentials.h. Reconnects
// whenever the association drops.
namespace wifi_station {

// Requires NVS, esp_netif and the default event loop to be initialized.
esp_err_t Start();

// Blocks until the station has its address.
void WaitConnected();

}  // namespace wifi_station
