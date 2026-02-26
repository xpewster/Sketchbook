idf_component_register(
    SRCS "main.cpp" "display.cpp" "network.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event esp_netif driver esp_lcd esp_timer LovyanGFX
    PRIV_REQUIRES esp_psram
)
