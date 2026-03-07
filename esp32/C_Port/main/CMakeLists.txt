idf_component_register(
    SRCS
        "main.cpp"
        "display.cpp"
        "network.cpp"
        "storage.cpp"
        "gif.cpp"
        "flash.cpp"
        "postprocess.cpp"
    INCLUDE_DIRS "."
    REQUIRES 
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        driver
        esp_lcd
        esp_timer
        esp_phy
        LovyanGFX
        lwip
        fatfs
        wear_levelling
        esp_tinyusb
        AnimatedGIF
    PRIV_REQUIRES esp_psram
)
