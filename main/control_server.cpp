#include "control_server.hpp"

#include <algorithm>
#include <array>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "control";
constexpr uint32_t kStack = 8192;
// Lets the response reach the client before the reboot drops the connection.
constexpr TickType_t kRebootDelay = pdMS_TO_TICKS(500);

esp_err_t SendStatus(httpd_req_t* req, const char* status, const char* body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, body);
}

}  // namespace

esp_err_t ControlServer::Start() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = kStack;
  config.lru_purge_enable = true;
  if (const esp_err_t err = httpd_start(&server_, &config); err != ESP_OK) return err;

  const httpd_uri_t ota = {
      .uri = "/ota", .method = HTTP_POST, .handler = HandleOta, .user_ctx = this};
  const httpd_uri_t reset = {
      .uri = "/reset", .method = HTTP_POST, .handler = HandleReset, .user_ctx = this};
  httpd_register_uri_handler(server_, &ota);
  httpd_register_uri_handler(server_, &reset);
  ESP_LOGI(kTag, "HTTP control server on port %u", config.server_port);
  return ESP_OK;
}

esp_err_t ControlServer::HandleReset(httpd_req_t* req) {
  auto* self = static_cast<ControlServer*>(req->user_ctx);
  const esp_err_t err = self->link_.Reset();
  if (err == ESP_ERR_INVALID_STATE) {
    return SendStatus(req, "503 Service Unavailable", "printer not connected\n");
  }
  if (err != ESP_OK) return SendStatus(req, "500 Internal Server Error", "reset failed\n");
  return SendStatus(req, "200 OK", "printer reset\n");
}

esp_err_t ControlServer::HandleOta(httpd_req_t* req) {
  if (req->content_len == 0) {
    return SendStatus(req, "411 Length Required", "send the image with a Content-Length\n");
  }
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t ota = 0;
  if (const esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
      err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin: %s", esp_err_to_name(err));
    // ESP_ERR_OTA_ROLLBACK_INVALID_STATE: the running image has not been marked valid yet.
    return SendStatus(req, "409 Conflict", "OTA not possible now\n");
  }
  ESP_LOGI(kTag, "OTA: %u bytes to %s", req->content_len, target->label);

  static std::array<char, 4096> buf;
  size_t remaining = req->content_len;
  while (remaining > 0) {
    const int n = httpd_req_recv(req, buf.data(), std::min(remaining, buf.size()));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      ESP_LOGE(kTag, "OTA: receive failed with %u bytes left", remaining);
      esp_ota_abort(ota);
      return ESP_FAIL;  // Closes the connection.
    }
    if (const esp_err_t err = esp_ota_write(ota, buf.data(), n); err != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_write: %s", esp_err_to_name(err));
      esp_ota_abort(ota);
      return SendStatus(req, "400 Bad Request", "image rejected\n");
    }
    remaining -= n;
  }
  if (const esp_err_t err = esp_ota_end(ota); err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_end: %s", esp_err_to_name(err));
    return SendStatus(req, "400 Bad Request", "image failed validation\n");
  }
  if (const esp_err_t err = esp_ota_set_boot_partition(target); err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
    return SendStatus(req, "500 Internal Server Error", "could not select new image\n");
  }
  SendStatus(req, "200 OK", "image written, rebooting\n");
  ESP_LOGI(kTag, "OTA complete, rebooting");
  vTaskDelay(kRebootDelay);
  esp_restart();
}
