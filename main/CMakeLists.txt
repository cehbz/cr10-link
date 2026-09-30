if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/credentials.h")
    message(FATAL_ERROR "main/credentials.h missing: copy credentials.h.template to main/credentials.h and fill it in")
endif()

idf_component_register(
    SRCS "main.cpp" "ftdi_link.cpp" "console_server.cpp" "control_server.cpp" "wifi_station.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app_update esp_event esp_http_server esp_netif esp_wifi lwip nvs_flash
)

