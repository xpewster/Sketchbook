#include "display.h"
#include "network.h"
#include "protocol.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

// ============================================================
// Network Task — runs on Core 0
// ============================================================
// Receives frames over TCP and pushes them to the display.

static void network_task(void* arg) {
    uint16_t* framebuf = (uint16_t*)arg;
    ESP_LOGI(TAG, "Network task started on core %d", xPortGetCoreID());

    wifi_init();
    network_init();
    tcp_server_start(framebuf);  // Blocks forever (accept loop)

    network_cleanup();
    vTaskDelete(NULL);
}

// ============================================================
// App Main
// ============================================================

extern "C" void app_main(void) {
    // NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Log memory
    ESP_LOGI(TAG, "Free PSRAM: %lu KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Free internal: %lu KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);

    // Step 1: Init ST7701S controller via IO expander
    // Must happen before gfx.init() since LovyanGFX doesn't know
    // about the IO expander.
    if (!init_st7701s()) {
        ESP_LOGE(TAG, "ST7701S init failed, halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Step 2: Init LovyanGFX (sets up RGB panel, allocates framebuffer)
    gfx.init();
    gfx.setColorDepth(16);  // RGB565
    gfx.setSwapBytes(true);  // Match RGB565 endianness to ST7701S expectations
    gfx.fillScreen(0);      // Black

    ESP_LOGI(TAG, "Display ready : %dx%d", gfx.width(), gfx.height());

    // Step 3: Allocate recv buffer in PSRAM for network frames
    uint16_t* framebuf = (uint16_t*)heap_caps_calloc(
        FRAME_PIXELS, sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!framebuf) {
        ESP_LOGE(TAG, "Failed to allocate recv buffer (%d KB)", FRAME_BYTES / 1024);
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Recv buffer allocated (%d KB in PSRAM)", FRAME_BYTES / 1024);

    // Step 4: Launch network task on Core 0
    // app_main runs on Core 0 by default, but we create an explicit task
    // with a large stack for the TCP recv loop.
    xTaskCreatePinnedToCore(
        network_task,
        "network",
        8192,           // Stack size (bytes)
        framebuf,       // Pass recv buffer as arg
        5,              // Priority
        NULL,
        0               // Core 0
    );

    ESP_LOGI(TAG, "System running. Core 1 free for future flash mode compositing.");

    // app_main returns — FreeRTOS scheduler continues running tasks
}
