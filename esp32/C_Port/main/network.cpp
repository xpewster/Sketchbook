#include "network.h"
#include "display.h"  // for gfx, FRAME_WIDTH/HEIGHT, display_flush_cache

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_phy_init.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "esp_timer.h"
#include <cstring>
#include <cerrno>

#include "postprocess.h"
#include "flash.h"
#include "storage.h"
#include "nvsm.h"


static const char* TAG = "network";

// Buffer for receiving RLE-compressed rects
uint8_t* rle_buf;

// Last received flash data (accessible after recv_frame returns OK for flash)
FlashDataHeader g_last_flash_data = {};

// Flash mode manager (created on mode switch)
static FlashModeManager s_flash_mgr;

// ============================================================
// Idle/Loading GIF
// ============================================================
// The idle GIF animation needs to run independently of the network task
// because wifi_init() blocks for seconds. A separate task on Core 1 keeps the animation smooth.
//
// Lifecycle:
//   idle_gif_load_and_show()  — called from main.cpp, loads GIF, renders
//                                first frame, starts animation task
//   idle_gif_stop()           — stops animation (client connected, or
//                                switching to flash/streaming display)
//   idle_gif_start()          — resumes animation (disconnect → idle)
//   idle_gif_freeze(true)     — suspends task during heavy PSRAM I/O
//                                (flash asset loading) to reduce bus contention
//   idle_gif_freeze(false)    — resumes task after PSRAM-heavy work

static Sprite       s_idle_gif;
static AnimatedGIF  s_idle_decoder;     // Own decoder — internal SRAM
static bool         s_idle_loaded = false;
static uint16_t*    s_idle_dma_visible = nullptr;
static TaskHandle_t s_idle_task = nullptr;
static bool         s_idle_running = false;  // True when task should animate

/// Load the appropriate loading GIF based on saved mode.
static void load_idle_gif(uint8_t saved_mode) {
    if (!storage_available()) {
        ESP_LOGW(TAG, "Filesystem not available, cannot load idle GIF");
        return;
    }

    const char* primary = nullptr;
    const char* fallback = FLASH_DEFAULT_LOADING_GIF;

    if (saved_mode == proto::MODE_FLASH) {
        primary = FLASH_ASSETS_DIR "/loading.gif";
    } else {
        primary = FLASH_DEFAULT_LOADING_GIF;
    }

    if (load_sprite(s_idle_gif, primary, 0, 0)) {
        ESP_LOGI(TAG, "Loaded idle GIF: %s (%dx%d)", primary, s_idle_gif.w, s_idle_gif.h);
    } else if (primary != fallback && load_sprite(s_idle_gif, fallback, 0, 0)) {
        ESP_LOGI(TAG, "Loaded fallback idle GIF: %s (%dx%d)", fallback, s_idle_gif.w, s_idle_gif.h);
    } else {
        ESP_LOGW(TAG, "No idle GIF available");
        return;
    }

    // Decode first frame
    if (s_idle_gif.is_gif()) {
        s_idle_gif.gif.bind(s_idle_decoder);
        s_idle_gif.gif.next_frame(s_idle_decoder);
        s_idle_gif.pixels = s_idle_gif.gif.pixels();
    }

    s_idle_loaded = true;
}

/// Render the current idle GIF frame to the DMA framebuffer with byte-swap.
static void idle_render_frame(uint16_t* dma_visible) {
    if (!s_idle_loaded || !s_idle_gif.pixels || !dma_visible) return;

    const uint16_t* src = s_idle_gif.pixels;
    int sw = s_idle_gif.w;
    int sh = s_idle_gif.h;
    int cw = (sw < FRAME_WIDTH)  ? sw : FRAME_WIDTH;
    int ch = (sh < FRAME_HEIGHT) ? sh : FRAME_HEIGHT;

    for (int y = 0; y < ch; y++) {
        const uint16_t* srow = &src[y * sw];
        uint16_t* drow = &dma_visible[y * DISP_STRIDE];
        for (int x = 0; x < cw; x++) {
            drow[x] = pp_pixel(srow[x]);
        }
    }
    display_flush_cache();
}

/// Core 1 task: animate idle GIF continuously while s_idle_running is true.
static void idle_gif_task(void*) {
    while (true) {
        if (s_idle_running && s_idle_loaded && s_idle_gif.is_gif() && s_idle_dma_visible) {
            s_idle_gif.gif.bind(s_idle_decoder);
            int delay_ms = s_idle_gif.gif.next_frame(s_idle_decoder);
            s_idle_gif.pixels = s_idle_gif.gif.pixels();

            idle_render_frame(s_idle_dma_visible);

            vTaskDelay(pdMS_TO_TICKS((delay_ms > 3) ? delay_ms : 3));
        } else {
            // Not running — sleep and check again
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// --- Public API ---

void idle_gif_load_and_show(uint16_t* dma_visible) {
    s_idle_dma_visible = dma_visible;

    uint8_t saved_mode = load_saved_mode();
    ESP_LOGI(TAG, "Saved mode: %s", saved_mode == proto::MODE_FLASH ? "flash" : "streaming");
    load_idle_gif(saved_mode);

    // Render first frame immediately (before task starts)
    idle_render_frame(dma_visible);

    // Start animation task on Core 1
    s_idle_running = true;
    if (!s_idle_task) {
        xTaskCreatePinnedToCore(
            idle_gif_task,
            "idle_gif",
            4096,
            nullptr,
            3,              // Lower priority than network (5) and composite helper (5)
            &s_idle_task,
            1               // Core 1
        );
        ESP_LOGI(TAG, "Idle GIF task started on Core 1");
    }
}

void idle_gif_start() {
    s_idle_running = true;
}

void idle_gif_stop() {
    s_idle_running = false;
}

void idle_gif_freeze(bool frozen) {
    if (!s_idle_task) return;
    if (frozen) {
        vTaskSuspend(s_idle_task);
        ESP_LOGD(TAG, "Idle GIF task suspended");
    } else {
        vTaskResume(s_idle_task);
        ESP_LOGD(TAG, "Idle GIF task resumed");
    }
}

// ============================================================
// WiFi
// ============================================================

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT     = BIT1;
static int s_retry_num = 0;
static const int MAX_RETRY = 1000;
static bool pause_connect = false;

static void wifi_event_handler(void* arg, esp_event_base_t base,
                                int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && !pause_connect) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && !pause_connect) {
        // if (s_retry_num < MAX_RETRY) {
            int delay_ms = 500; // MIN(1000 * (1 << MIN(s_retry_num, 4)), 30000);
            ESP_LOGI(TAG, "WiFi disconnected, retry #%d in %dms", s_retry_num + 1, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "WiFi retry %d/%d", s_retry_num, MAX_RETRY);
        // }
        // else {
        //     xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        // }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init() {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, CONFIG_ESP_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, CONFIG_ESP_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to %s...", CONFIG_ESP_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        // Disable modem sleep for streaming throughput.
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
    }
}

void reconnect_wifi() {
    ESP_LOGI(TAG, "Reconnecting WiFi...");
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_wifi_disconnect();
    pause_connect = true;  // Prevent auto-reconnect during disconnect
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait a bit for disconnect to complete
    pause_connect = false;
    esp_wifi_connect();
    // wifi_event_handler will call esp_wifi_connect() on WIFI_EVENT_STA_DISCONNECTED
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi reconnected");
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else {
        ESP_LOGE(TAG, "WiFi reconnect failed");
    }
}

// ============================================================
// TCP Helpers
// ============================================================

static bool recv_exact(int sock, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    size_t remaining = len;
    while (remaining > 0) {
        int n = recv(sock, p, remaining, 0);
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

// Timeout-aware recv for the first byte of a message.
// Returns 1 on success, 0 on timeout, -1 on error/disconnect.
static int recv_msg_type(int sock, uint8_t* msg_type) {
    int n = recv(sock, msg_type, 1, 0);
    if (n == 1) return 1;
    if (n == 0) return -1;  // Peer closed
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;  // Timeout
    return -1;  // Error
}

static void send_ack(int sock) {
    uint8_t ack = 0x06;
    send(sock, &ack, 1, 0);
}

// ============================================================
// Init / Cleanup
// ============================================================

void network_init() {
    // Allocate RLE buffer for dirty rects (worst case: no compression)
    rle_buf = (uint8_t*)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rle_buf) {
        rle_buf = (uint8_t*)malloc(FRAME_BYTES);
    }
    if (!rle_buf) {
        ESP_LOGE(TAG, "Failed to allocate RLE buffer");
    }
    init_color_luts();
    s_flash_mgr = FlashModeManager();
}

void time_sync_init() {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
}

bool preload_flash_assets() {
    if (s_flash_mgr.is_loaded()) return true;
    if (!storage_available()) return false;

    ESP_LOGI(TAG, "Pre-loading flash assets (PSRAM free: %lu KB)...",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    bool ok = s_flash_mgr.init(FLASH_CONFIG_FILE);

    ESP_LOGI(TAG, "Flash preload %s (PSRAM free: %lu KB)",
             ok ? "OK" : "FAILED",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    return ok;
}

void network_cleanup() {
    free(rle_buf);
}

// ============================================================
// Display Helpers — direct DMA framebuffer access
// ============================================================
// Instead of gfx.pushImage() (which memcpys 450KB from our buffer into
// LovyanGFX's internal DMA buffer), we composite/copy directly into the
// DMA buffer and flush the CPU cache so the LCD peripheral sees our writes.

/// Copy streaming-mode framebuf (FRAME_WIDTH stride) into DMA visible area (DISP_STRIDE stride).
/// Swaps bytes for LCD DMA byte order (same as setSwapBytes(true) in LovyanGFX).
static void blit_to_display(const uint16_t* src, uint16_t* dma_visible) {
    for (int y = 0; y < FRAME_HEIGHT; y++) {
        const uint16_t* srow = &src[y * FRAME_WIDTH];
        uint16_t* drow = &dma_visible[y * DISP_STRIDE];
        for (int x = 0; x < FRAME_WIDTH; x++) {
            drow[x] = pp_pixel(srow[x]);
        }
    }
    display_flush_cache();
}

/// Clear entire DMA framebuffer (including overscan) to black.
static void clear_display(uint16_t* dma_fb) {
    memset(dma_fb, 0, DISP_STRIDE * DISP_HEIGHT * sizeof(uint16_t));
    display_flush_cache();
}

// ============================================================
// Dirty Rect Receive Helper
// ============================================================
// Receives rect_count dirty rects into target buffer (stride = FRAME_WIDTH).
// Used by both MSG_DIRTY_RECTS and MSG_FLASH_DATA.
// Optionally reports the bounding y-range of all received rects for dirty tracking.

static RecvResult receive_dirty_rects(int sock, uint16_t* target,
                                       uint8_t rect_count, ColorMode colorMode,
                                       uint16_t rle_escape_color,
                                       int* out_y_min = nullptr,
                                       int* out_y_max = nullptr) {
    DirtyRect rects[256];
    if (!recv_exact(sock, rects, rect_count * sizeof(DirtyRect))) {
        ESP_LOGE(TAG, "Failed to receive dirty rects");
        return RecvResult::ERROR;
    }

    // Compute bounding y-range across all rects
    if (out_y_min && out_y_max) {
        int ymin = FRAME_HEIGHT, ymax = 0;
        for (int i = 0; i < rect_count; i++) {
            if (rects[i].y < ymin) ymin = rects[i].y;
            int bottom = rects[i].y + rects[i].h;
            if (bottom > ymax) ymax = bottom;
        }
        *out_y_min = ymin;
        *out_y_max = ymax;
    }
    // ESP_LOGI(TAG, "Receiving %d dirty rects (ColorMode=%d, EscapeColor=0x%04X)",
    //          rect_count, colorMode, rle_escape_color);

    for (int i = 0; i < rect_count; i++) {
        DirtyRect& r = rects[i];
        // if (r.w == FRAME_WIDTH) {
        //     uint16_t* dst = target + r.y * FRAME_WIDTH;
        //     if (!recv_exact(sock, dst, r.w * r.h * 2))
        //         return RecvResult::ERROR;
        // } else {
        uint32_t runLengthEncodingSize;
        if (!recv_exact(sock, &runLengthEncodingSize, sizeof(runLengthEncodingSize))) {
            ESP_LOGE(TAG, "Failed to receive RLE size");
            return RecvResult::ERROR;
        }
        // ESP_LOGI(TAG, "Rect %d: x=%d y=%d w=%d h=%d RLE size=%d bytes ColorMode=%d EscapeColor=0x%04X",
        //          i, r.x, r.y, r.w, r.h, runLengthEncodingSize, header.colorMode, rle_escape_color);


        const int bpp = bitsPerPixel(colorMode);

        if (runLengthEncodingSize == 0) {
            // Raw packed data
            if (colorMode == ColorMode::RGB565) {
                // Fast path: recv directly into framebuffer row by row
                for (int row = 0; row < r.h; row++) {
                    uint16_t* dst = target + (r.y + row) * FRAME_WIDTH + r.x;
                    if (!recv_exact(sock, dst, r.w * 2)) {
                        ESP_LOGE(TAG, "Failed to receive uncompressed rect row");
                        return RecvResult::ERROR;
                    }
                }
            } else {
                // Sub-byte modes: recv packed bitstream, then unpack with conversion
                size_t rawBytes = packedByteSize((size_t)r.w * r.h, colorMode);
                if (!recv_exact(sock, rle_buf, rawBytes)) {
                    ESP_LOGE(TAG, "Failed to receive raw rect data");
                    return RecvResult::ERROR;
                }
                BitReader reader(rle_buf, rawBytes);
                int total = r.w * r.h;
                int px_x = 0, px_y = 0;
                for (int j = 0; j < total; j++) {
                    uint16_t px = reader.read(bpp);
                    writePixel(target, r, px_x, px_y, toRGB565(px, colorMode));
                }
            }
        } else {
            int px_x = 0, px_y = 0;

            if (colorMode == ColorMode::RGB565) {
                // Fast path: byte-aligned little-endian uint16_t stream
                if (!recv_exact(sock, rle_buf, runLengthEncodingSize)) {
                    ESP_LOGE(TAG, "Failed to receive RLE rect data");
                    return RecvResult::ERROR;
                }
                uint16_t* p = (uint16_t*)rle_buf;
                uint16_t* p_end = (uint16_t*)(rle_buf + runLengthEncodingSize);

                while (px_y < r.h) {
                    if (p >= p_end) { ESP_LOGE(TAG, "RLE buffer overrun"); ESP_LOGI(TAG, "RLEsize=%d", runLengthEncodingSize); return RecvResult::ERROR; }
                    uint16_t color = *p++;
                    uint16_t count = 1;

                    if (color == rle_escape_color) {
                        if (p + 2 > p_end) { ESP_LOGE(TAG, "RLE buffer overrun during escape sequence"); ESP_LOGI(TAG, "RLEsize=%d", runLengthEncodingSize); return RecvResult::ERROR; }
                        color = *p++;
                        count = *p++;
                    }

                    for (uint16_t j = 0; j < count; j++) {
                        target[(r.y + px_y) * FRAME_WIDTH + (r.x + px_x)] = color;
                        if (++px_x >= r.w) {
                            px_x = 0;
                            px_y++;
                            if (px_y >= r.h && j + 1 < count) {
                                ESP_LOGE(TAG, "RLE pixel overrun");
                                return RecvResult::ERROR;
                            }
                        }
                    }
                }
            } else {
                // Sub-byte modes: N-bit aligned bitstream
                if (!recv_exact(sock, rle_buf, runLengthEncodingSize)) {
                    ESP_LOGE(TAG, "Failed to receive RLE rect data");
                    return RecvResult::ERROR;
                }
                BitReader reader(rle_buf, runLengthEncodingSize);

                while (px_y < r.h) {
                    uint16_t color = reader.read(bpp);
                    uint16_t count = 1;

                    if (color == rle_escape_color) {
                        color = reader.read(bpp);
                        count = reader.read(bpp);
                    }

                    uint16_t rgb565val = toRGB565(color, colorMode);
                    for (uint16_t j = 0; j < count; j++) {
                        target[(r.y + px_y) * FRAME_WIDTH + (r.x + px_x)] = rgb565val;
                        if (++px_x >= r.w) {
                            px_x = 0;
                            px_y++;
                            if (px_y >= r.h && j + 1 < count) {
                                ESP_LOGE(TAG, "RLE pixel overrun");
                                return RecvResult::ERROR;
                            }
                        }
                    }
                }
            }
        }
    }
    return RecvResult::OK;
}

RecvResult recv_frame(int sock, uint16_t* framebuf, uint8_t& current_mode) {
    uint8_t msg_type;
    int rc = recv_msg_type(sock, &msg_type);
    if (rc == 0) return RecvResult::TIMEOUT;
    if (rc < 0) {
        ESP_LOGE(TAG, "Failed to receive message type");
        return RecvResult::DISCONNECTED;
    }

    switch (msg_type) {

    case proto::MSG_FULL_FRAME: {
        ColorMode colorMode;
        if (!recv_exact(sock, (uint8_t*)&colorMode, 1)) {
            ESP_LOGE(TAG, "Failed to receive full frame color mode");
            return RecvResult::ERROR;
        }
        if (colorMode == ColorMode::RGB565) {
            if (!recv_exact(sock, framebuf, FRAME_BYTES)) {
                ESP_LOGE(TAG, "Failed to receive full frame");
                return RecvResult::ERROR;
            }
        } else {
            uint32_t runLengthEncodingSize;
            if (!recv_exact(sock, (uint8_t*)&runLengthEncodingSize, sizeof(runLengthEncodingSize))) {
                ESP_LOGE(TAG, "Failed to receive full frame RLE size");
                return RecvResult::ERROR;
            }
            size_t rawBytes = packedByteSize(FRAME_PIXELS, colorMode);
            if (!recv_exact(sock, rle_buf, rawBytes)) {
                ESP_LOGE(TAG, "Failed to receive full frame packed data");
                return RecvResult::ERROR;
            }
            BitReader reader(rle_buf, rawBytes);
            for (int i = 0; i < FRAME_PIXELS; i++) {
                uint16_t px = reader.read(bitsPerPixel(colorMode));
                framebuf[i] = toRGB565(px, colorMode);
            }
        }
        ESP_LOGI(TAG, "Received full frame");
        send_ack(sock);
        return RecvResult::OK;
    }

    case proto::MSG_DIRTY_RECTS: {
        DirtyRectsHeader header;
        if (!recv_exact(sock, &header, sizeof(header))) {
            ESP_LOGE(TAG, "Failed to receive dirty rects header");
            return RecvResult::ERROR;
        }
        ColorMode colorMode = static_cast<ColorMode>(header.colorMode);

        RecvResult rr = receive_dirty_rects(sock, framebuf,
                                             header.rectCount, colorMode,
                                             header.rleEscapeColor);
        if (rr != RecvResult::OK) return rr;

        send_ack(sock);
        return RecvResult::OK;
    }

    case proto::MSG_FLASH_DATA: {
        FlashDataHeader fh;
        if (!recv_exact(sock, &fh, sizeof(fh))) {
            ESP_LOGE(TAG, "Failed to receive flash data header");
            return RecvResult::ERROR;
        }

        g_last_flash_data = fh;

        // Receive dirty rects into flash manager's stream buffer
        if (fh.rect_count > 0) {
            ColorMode mode = static_cast<ColorMode>(fh.color_mode);
            uint16_t* target = s_flash_mgr.is_loaded() ? s_flash_mgr.stream_pixels() : nullptr;

            if (target) {
                int y_min = 0, y_max = 0;
                RecvResult rr = receive_dirty_rects(sock, target,
                                                     fh.rect_count, mode,
                                                     fh.rle_escape_color,
                                                     &y_min, &y_max);
                if (rr != RecvResult::OK) {
                    ESP_LOGE(TAG, "Failed to receive flash dirty rects");
                    return rr;
                }
                // Mark only the affected rows for recomposite
                if (y_max > y_min) {
                    s_flash_mgr.mark_stream_rows_dirty(y_min, y_max - y_min);
                }
            } else {
                // Flash manager not ready — drain the rect data so the stream stays in sync
                ESP_LOGW(TAG, "Flash data received but no flash manager — draining");
                DirtyRect rects[256];
                if (!recv_exact(sock, rects, fh.rect_count * sizeof(DirtyRect))) {
                    ESP_LOGE(TAG, "Failed to drain flash dirty rect headers");
                    return RecvResult::ERROR;
                }
                for (int i = 0; i < fh.rect_count; i++) {
                    uint32_t rleSize;
                    if (!recv_exact(sock, &rleSize, sizeof(rleSize))) {
                        ESP_LOGE(TAG, "Failed to drain flash dirty rect RLE size");
                        return RecvResult::ERROR;
                    }
                    size_t bytes;
                    if (rleSize > 0) {
                        bytes = rleSize;
                    } else if (mode == ColorMode::RGB565) {
                        bytes = (size_t)rects[i].w * rects[i].h * 2;
                    } else {
                        bytes = packedByteSize((size_t)rects[i].w * rects[i].h, mode);
                    }
                    if (bytes > 0 && !recv_exact(sock, rle_buf, bytes)) {
                        ESP_LOGE(TAG, "Failed to drain flash dirty rect pixel data");
                        return RecvResult::ERROR;
                    }
                }
            }
        }

        if (s_flash_mgr.is_loaded()) {
            s_flash_mgr.update_from_data(fh);
        }

        send_ack(sock);
        return RecvResult::OK;
    }

    case proto::MSG_NO_CHANGE: {
        send_ack(sock);
        return RecvResult::NO_CHANGE;
    }

    case proto::MSG_SET_MODE: {
        uint8_t mode;
        if (!recv_exact(sock, &mode, 1)) {
            ESP_LOGE(TAG, "Failed to receive mode");
            return RecvResult::ERROR;
        }
        current_mode = mode;
        send_ack(sock);
        ESP_LOGI(TAG, "Mode changed to %d", mode);
        return RecvResult::MODE_CHANGED;
    }

    case proto::MSG_RESET: {
        ESP_LOGW(TAG, "Reset requested");
        send_ack(sock);
        return RecvResult::RESET_REQUESTED;
    }

    case proto::MSG_RECONNECT: {
        ESP_LOGW(TAG, "Reconnect requested");
        send_ack(sock);
        return RecvResult::RECONNECT_REQUESTED;
    }

    case proto::MSG_SET_BRIGHTNESS: {
        uint8_t brightness, _save_brightness;
        if (!recv_exact(sock, &brightness, 1)) {
            ESP_LOGE(TAG, "Failed to receive brightness");
            return RecvResult::ERROR;
        }
        if (!recv_exact(sock, &_save_brightness, 1)) {
            ESP_LOGE(TAG, "Failed to receive save_brightness");
            return RecvResult::ERROR;
        }
        ESP_LOGI(TAG, "Brightness set to %d", brightness);
        if (pp_get_brightness() != brightness) {
            pp_set_brightness(brightness);
            save_brightness(_save_brightness);
            // Force full recomposite — existing DMA buffer has old brightness
            if (s_flash_mgr.is_loaded()) {
                s_flash_mgr.mark_all_dirty();
            }
        } else if (pp_get_brightness() != _save_brightness) {
            save_brightness(_save_brightness);
        }
        send_ack(sock);
        return RecvResult::NO_CHANGE;
    }

    default:
        ESP_LOGW(TAG, "Unknown message type: 0x%02X", msg_type);
        return RecvResult::ERROR;
    }
}

// ============================================================
// TCP Server (network_task)
// ============================================================

void tcp_server_start(uint16_t* framebuf) {
    s_retry_num = 0;
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Large recv buffer for throughput
    int bufsize = 65535;
    setsockopt(server_sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed");
        close(server_sock);
        return;
    }

    if (listen(server_sock, 1) < 0) {
        ESP_LOGE(TAG, "listen() failed");
        close(server_sock);
        return;
    }

    // Set socket to nonblocking
    fcntl(server_sock, F_SETFL, O_NONBLOCK);

    ESP_LOGI(TAG, "TCP server listening on port %d", TCP_PORT);

    // Get direct access to the LCD DMA framebuffer via Bus_RGB::getDMABuffer().
    // This lets us write pixels directly where the LCD peripheral reads,
    // eliminating the 450KB pushImage memcpy every frame.
    uint16_t* dma_fb = gfx.getFrameBuffer();
    uint16_t* dma_visible = gfx.getVisibleBuffer();
    if (!dma_fb || !dma_visible) {
        ESP_LOGE(TAG, "Failed to get DMA framebuffer — falling back to pushImage");
    } else {
        ESP_LOGI(TAG, "Direct DMA framebuffer access enabled (fb=%p, visible=%p, stride=%d)",
                 dma_fb, dma_visible, DISP_STRIDE);
    }

    // Idle GIF was already loaded and rendered by main.cpp before the network
    // task launched. The DMA buffer still has that frame showing.

    // Disconnect grace periods: keep showing last content before switching to idle GIF.
    // Flash mode gets a longer period since animations keep running independently.
    // Streaming mode shows a frozen last frame briefly before the idle GIF.
    constexpr int64_t FLASH_DISCONNECT_GRACE_US    = 30 * 1000000LL;
    constexpr int64_t STREAMING_DISCONNECT_GRACE_US = 5 * 1000000LL;
    int64_t disconnect_time = 0;
    bool showing_idle = true;
    uint8_t disconnected_mode = proto::MODE_STREAMING;  // Mode at time of disconnect

    while (true) {
        // --- Accept loop: animate idle GIF while waiting for client ---
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = -1;

        while (client_sock < 0) {
            client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock >= 0) break;

            int64_t now = esp_timer_get_time();

            if (!showing_idle && disconnect_time > 0) {
                int64_t grace = (disconnected_mode == proto::MODE_FLASH)
                    ? FLASH_DISCONNECT_GRACE_US : STREAMING_DISCONNECT_GRACE_US;
                if ((now - disconnect_time) >= grace) {
                    ESP_LOGI(TAG, "Disconnect grace period expired, showing idle GIF");
                    showing_idle = true;
                    if (dma_fb) clear_display(dma_fb);
                    idle_gif_start();  // Resume Core 1 animation task
                }
            }

            if (!showing_idle && disconnected_mode == proto::MODE_FLASH
                       && s_flash_mgr.is_loaded() && dma_visible) {
                // During flash grace period: keep animating on this core
                s_flash_mgr.tick_and_composite(dma_visible, DISP_STRIDE);
                display_flush_cache();
                vTaskDelay(pdMS_TO_TICKS(5));
            } else {
                // Idle GIF animating on Core 1, or streaming grace (frozen frame).
                // Either way, nothing for this core to do.
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

        ESP_LOGI(TAG, "Client connected from " IPSTR,
                 IP2STR((esp_ip4_addr_t*)&client_addr.sin_addr));
        showing_idle = false;
        disconnect_time = 0;
        idle_gif_stop();  // Stop Core 1 animation — we own the DMA buffer now

        // TCP_NODELAY: disable Nagle's algorithm.
        opt = 1;
        setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        // Timeout
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint8_t current_mode = proto::MODE_STREAMING;
        int frame_count = 0;
        int64_t fps_start = esp_timer_get_time();
        int64_t recv_us_total = 0;
        int64_t blit_us_total = 0;
        int64_t composite_us_total = 0;
        int     dirty_rows_total = 0;

        while (true) {
            int64_t t0 = esp_timer_get_time();
            RecvResult result = recv_frame(client_sock, framebuf, current_mode);
            int64_t t1 = esp_timer_get_time();

            if (result == RecvResult::OK) {

                if (current_mode == proto::MODE_FLASH && s_flash_mgr.is_loaded()) {
                    // Flash mode: composite directly into DMA framebuffer
                    int rows;
                    if (dma_visible) {
                        rows = s_flash_mgr.tick_and_composite(dma_visible, DISP_STRIDE);
                    } else {
                        rows = s_flash_mgr.tick_and_composite(framebuf);
                    }
                    dirty_rows_total += rows;
                }
                int64_t t2 = esp_timer_get_time();

                // Blit to display
                if (dma_visible) {
                    if (current_mode != proto::MODE_FLASH || !s_flash_mgr.is_loaded()) {
                        // Streaming mode: copy from recv buffer into DMA visible area
                        blit_to_display(framebuf, dma_visible);
                    } else {
                        // Flash mode: already composited into DMA buffer, just flush cache
                        display_flush_cache();
                    }
                } else {
                    // Fallback: use LovyanGFX pushImage
                    ESP_LOGW(TAG, "DMA framebuffer not available — using pushImage (slow)");
                    gfx.pushImage(DISP_OVERSCAN_LEFT, 0, FRAME_WIDTH, FRAME_HEIGHT, framebuf);
                }

                int64_t t3 = esp_timer_get_time();
                recv_us_total += (t1 - t0);
                composite_us_total += (t2 - t1);
                blit_us_total += (t3 - t2);
                frame_count++;
            } else if (result == RecvResult::NO_CHANGE) {
                int64_t t2 = t1, t3 = t1;
                if (current_mode == proto::MODE_FLASH && s_flash_mgr.is_loaded()) {
                    // Still animate and composite even with no new data
                    if (dma_visible) {
                        s_flash_mgr.tick_and_composite(dma_visible, DISP_STRIDE);
                        t2 = esp_timer_get_time();
                        display_flush_cache();
                    } else {
                        s_flash_mgr.tick_and_composite(framebuf);
                        t2 = esp_timer_get_time();
                        gfx.pushImage(DISP_OVERSCAN_LEFT, 0, FRAME_WIDTH, FRAME_HEIGHT, framebuf);
                    }
                }
                t3 = esp_timer_get_time();
                recv_us_total += (t1 - t0);
                composite_us_total += (t2 - t1);
                blit_us_total += (t3 - t2);
                frame_count++;
            } else if (result == RecvResult::TIMEOUT) {
                if (current_mode == proto::MODE_FLASH && s_flash_mgr.is_loaded()) {
                    // Timeout in flash mode: keep animating
                    if (dma_visible) {
                        s_flash_mgr.tick_and_composite(dma_visible, DISP_STRIDE);
                        display_flush_cache();
                    } else {
                        s_flash_mgr.tick_and_composite(framebuf);
                        gfx.pushImage(DISP_OVERSCAN_LEFT, 0, FRAME_WIDTH, FRAME_HEIGHT, framebuf);
                    }
                }
                // In streaming mode, timeout means PC stopped sending — just wait
            } else if (result == RecvResult::MODE_CHANGED) {
                ESP_LOGI(TAG, "Switching to mode %d", current_mode);
                save_mode(current_mode);

                if (current_mode == proto::MODE_FLASH) {
                    if (!s_flash_mgr.is_loaded()) {
                        ESP_LOGI(TAG, "Flash mode: loading assets...");
                        s_flash_mgr.init(FLASH_CONFIG_FILE);
                    }
                }
            } else if (result == RecvResult::NO_OP_AFTER) {
                // No-op — nothing to do
            } else if (result == RecvResult::RESET_REQUESTED) {
                ESP_LOGI(TAG, "Reset requested — restarting ESP32");
                esp_phy_erase_cal_data_in_nvs(); // Reset PHY calibration data. Sometimes needed to fix WiFi throughput issues
                esp_restart();
                break;
            } else if (result == RecvResult::RECONNECT_REQUESTED) {
                ESP_LOGI(TAG, "Reconnect requested — restarting connection");
                close(client_sock);
                reconnect_wifi();
                break;
            } else {
                // Disconnected or error
                ESP_LOGE(TAG, "recv_frame() failed with result %d, closing connection", (int)result);
                break;
            }

            // FPS + breakdown tracking
            int64_t now = esp_timer_get_time();
            int64_t elapsed = now - fps_start;
            if (elapsed >= 5000000) {  // 5 seconds
                float fps = (float)frame_count * 1000000.0f / elapsed;
                float avg_recv_ms = frame_count ? (recv_us_total / 1000.0f / frame_count) : 0;
                float avg_composite_ms = frame_count ? (composite_us_total / 1000.0f / frame_count) : 0;
                float avg_blit_ms = frame_count ? (blit_us_total / 1000.0f / frame_count) : 0;
                float throughput_mbps = frame_count ? (frame_count * FRAME_BYTES * 8.0f / elapsed) : 0;
                float avg_dirty_rows = frame_count ? ((float)dirty_rows_total / frame_count) : 0;
                ESP_LOGI(TAG, "Averages: FPS: %.1f | recv: %.1fms | composite: %.1fms (%.0f dirty rows) | blit: %.1fms | %.1f Mbps (%d frames)",
                         fps, avg_recv_ms, avg_composite_ms, avg_dirty_rows, avg_blit_ms, throughput_mbps, frame_count);
                frame_count = 0;
                recv_us_total = 0;
                composite_us_total = 0;
                dirty_rows_total = 0;
                blit_us_total = 0;
                fps_start = now;
            }
        }

        close(client_sock);
        ESP_LOGI(TAG, "Client disconnected");

        // Start disconnect grace period.
        // Flash mode: keep animating during the grace period.
        // Streaming mode: freeze last frame during the grace period.
        // After timeout, switch to idle GIF.
        disconnect_time = esp_timer_get_time();
        disconnected_mode = current_mode;
        showing_idle = false;

        if (current_mode == proto::MODE_FLASH && s_flash_mgr.is_loaded()) {
            ESP_LOGI(TAG, "Flash mode disconnect — grace period %.0fs",
                     FLASH_DISCONNECT_GRACE_US / 1000000.0f);
        } else {
            ESP_LOGI(TAG, "Streaming mode disconnect — grace period %.0fs",
                     STREAMING_DISCONNECT_GRACE_US / 1000000.0f);
        }
    }
}