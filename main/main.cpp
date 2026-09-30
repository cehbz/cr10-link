#include "credentials.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "cr10-link";

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(kTag, "%s starting", credentials::kHostname);
}
