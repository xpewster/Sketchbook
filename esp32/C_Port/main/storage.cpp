#include "storage.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "tinyusb.h"
#include "tinyusb_msc.h"              // v2.0 API (replaces deprecated tusb_msc_storage.h)
#include "tinyusb_default_config.h"    // TINYUSB_DEFAULT_CONFIG() macro
#include "tusb_cdc_acm.h"             // CDC-ACM for serial console
#include "tusb_console.h"             // esp_tusb_init_console()

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>

static const char* TAG = "storage";

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static tinyusb_msc_storage_handle_t s_storage_hdl = nullptr;
static std::atomic<bool> s_fs_available{false};
static SemaphoreHandle_t s_fs_sem = nullptr;

// Create reset callback for driver program to trigger
static void cdc_rx_callback(int itf, cdcacm_event_t *event) {
    uint8_t buf[64];
    size_t rx_size = 0;
    tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, buf, sizeof(buf), &rx_size);
    for (size_t i = 0; i < rx_size; i++) {
        if (buf[i] == 0xFF) {
            esp_restart();
        }
    }
}

// ============================================================
// USB MSC Event Callback (v2.0 signature)
// ============================================================
// In v2.0, the callback receives a handle, event with id enum,
// and a user argument — replacing the old mount_changed_data approach.

static void storage_event_cb(tinyusb_msc_storage_handle_t handle,
                             tinyusb_msc_event_t* event, void* arg) {
    switch (event->id) {
        case TINYUSB_MSC_EVENT_MOUNT_START:
            // USB host is taking over the storage
            ESP_LOGW(TAG, "USB host mounting — filesystem unavailable");
            s_fs_available.store(false);
            break;
        case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
            // USB host released storage, local VFS re-mounted
            ESP_LOGI(TAG, "USB host unmounted — filesystem available");
            s_fs_available.store(true);
            if (s_fs_sem) xSemaphoreGive(s_fs_sem);
            break;
        case TINYUSB_MSC_EVENT_MOUNT_FAILED:
            ESP_LOGE(TAG, "Local FAT re-mount failed after USB eject");
            break;
        case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
            ESP_LOGW(TAG, "Storage format required");
            break;
        default:
            break;
    }
}

// ============================================================
// Public API
// ============================================================

bool storage_init() {
    s_fs_sem = xSemaphoreCreateBinary();

    // Mount FAT partition with wear levelling for local file access
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        FLASH_MOUNT_POINT, "storage", &mount_config, &s_wl_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAT mount failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "FAT partition mounted at %s", FLASH_MOUNT_POINT);
    s_fs_available.store(true);

    // Create MSC storage backed by SPI flash (v2.0 API)
    tinyusb_msc_storage_config_t msc_cfg = {};
    msc_cfg.medium.wl_handle = s_wl_handle;

    err = tinyusb_msc_new_storage_spiflash(&msc_cfg, &s_storage_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC storage init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Register storage event callback
    tinyusb_msc_set_storage_callback(storage_event_cb, nullptr);

    // Install TinyUSB driver with default config
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(err));
        return false;
    }

    // Initialize CDC-ACM for serial console (composite CDC+MSC device)
    tinyusb_config_cdcacm_t acm_cfg = {};
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    err = tusb_cdc_acm_init(&acm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CDC-ACM init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Redirect stdout/stderr to TinyUSB CDC
    esp_tusb_init_console(TINYUSB_CDC_ACM_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Console redirect failed: %s", esp_err_to_name(err));
        // Non-fatal — MSC still works, just no serial output
    }

    // Register RX callback to handle USB reset signal from driver program
    tinyusb_cdcacm_register_callback(TINYUSB_CDC_ACM_0, CDC_EVENT_RX, &cdc_rx_callback);

    ESP_LOGI(TAG, "USB MSC active — drag files to %s via USB", FLASH_ASSETS_DIR);
    return true;
}

bool storage_available() {
    return s_fs_available.load();
}

bool storage_wait_available(int timeout_ms) {
    if (s_fs_available.load()) return true;
    ESP_LOGI(TAG, "Waiting for filesystem (USB host may have it mounted)...");
    if (xSemaphoreTake(s_fs_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return s_fs_available.load();
    }
    return false;
}
