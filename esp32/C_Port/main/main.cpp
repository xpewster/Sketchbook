#include "display.h"
#include "network.h"
#include "protocol.h"
#include "storage.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

// ============================================================
// Network Task — runs on Core 0
// ============================================================

static void network_task(void* arg) {
    uint16_t* framebuf = (uint16_t*)arg;
    ESP_LOGI(TAG, "Network task started on core %d", xPortGetCoreID());

    // network_init() already called from app_main before this task launched
    wifi_init();
    tcp_server_start(framebuf);  // Blocks forever (accept loop)

    network_cleanup();
    vTaskDelete(NULL);
}

// ============================================================
// App Main
// ============================================================

extern "C" void app_main(void) {
    // NVS (required for WiFi and mode persistence)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Log memory
    ESP_LOGI(TAG, "Free PSRAM: %lu KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Free internal: %lu KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);

    // Step 1: Initialize USB Mass Storage + FAT filesystem
    // This must happen before any assets are loaded from flash.
    if (!storage_init()) {
        ESP_LOGW(TAG, "Storage init failed — flash mode will be unavailable");
    } else {
        ESP_LOGI(TAG, "Storage ready. Drag flash assets to USB drive at %s", FLASH_ASSETS_DIR);
    }

    // Step 2: Init ST7701S controller via IO expander
    if (!init_st7701s()) {
        ESP_LOGE(TAG, "ST7701S init failed, halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Step 3: Init LovyanGFX (sets up RGB panel, allocates DMA framebuffer)
    gfx.init();
    gfx.setColorDepth(16);
    gfx.setSwapBytes(true);  // For pushImage fallback path
    gfx.fillScreen(0);
    ESP_LOGI(TAG, "Display ready: %dx%d", gfx.width(), gfx.height());

    // Step 4: Initialize network buffers
    network_init();

    // Step 5: Pre-load flash assets while screen is still black.
    // Heavy PSRAM I/O here causes LCD DMA glitching, but the screen is
    // blank so the user doesn't see it.
    preload_flash_assets();

    // Step 6: Load and display idle GIF.
    // Now that heavy PSRAM work is done, the idle GIF animates smoothly.
    // Animation task starts on Core 1 and runs through WiFi connect.
    {
        uint16_t* dma_visible = gfx.getVisibleBuffer();
        if (dma_visible) {
            idle_gif_load_and_show(dma_visible);
            ESP_LOGI(TAG, "Idle GIF displayed");
        }
    }

    // Step 7: Allocate recv/composite buffer in PSRAM
    uint16_t* framebuf = (uint16_t*)heap_caps_calloc(
        FRAME_PIXELS, sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!framebuf) {
        ESP_LOGE(TAG, "Failed to allocate recv buffer (%d KB)", FRAME_BYTES / 1024);
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Recv buffer allocated (%d KB in PSRAM)", FRAME_BYTES / 1024);

    // Step 8: Launch network task on Core 0
    // wifi_init() will block for seconds — idle GIF animates on Core 1 meanwhile.
    xTaskCreatePinnedToCore(
        network_task,
        "network",
        12288,
        framebuf,
        5,
        NULL,
        0               // Core 0
    );

    // Log post-init memory
    ESP_LOGI(TAG, "Post-init PSRAM: %lu KB free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Post-init internal: %lu KB free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    ESP_LOGI(TAG, "System running. USB MSC active. Awaiting TCP connection on port %d.", TCP_PORT);
}