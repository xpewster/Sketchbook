#include "network.h"
#include "display.h"  // for gfx, FRAME_WIDTH/HEIGHT, display_flush_cache

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "esp_timer.h"
#include <cstring>
#include <cerrno>

#include "flash.h"
#include "storage.h"

static const char* TAG = "network";

// Buffer for receiving RLE-compressed rects
uint8_t* rle_buf;

// Last received flash data (accessible after recv_frame returns OK for flash)
FlashDataHeader g_last_flash_data = {};

// Flash mode manager (created on mode switch)
static FlashModeManager s_flash_mgr;

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
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "WiFi retry %d/%d", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
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

// ============================================================
// Protocol: recv_frame
// ============================================================

static void send_ack(int sock) {
    uint8_t ack = 0x06;
    send(sock, &ack, 1, 0);
}

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

void network_cleanup() {
    free(rle_buf);
}

// ============================================================
// Display Helpers — direct DMA framebuffer access
// ============================================================
// Instead of gfx.pushImage() (which memcpys 450KB from our buffer into
// LovyanGFX's internal DMA buffer), we composite/copy directly into the
// DMA buffer and flush the CPU cache so the LCD peripheral sees our writes.
// This matches CircuitPython's approach (see DotClockFramebuffer.c).

/// Copy streaming-mode framebuf (FRAME_WIDTH stride) into DMA visible area (DISP_STRIDE stride).
static void blit_to_display(const uint16_t* src, uint16_t* dma_visible) {
    for (int y = 0; y < FRAME_HEIGHT; y++) {
        memcpy(&dma_visible[y * DISP_STRIDE], &src[y * FRAME_WIDTH], FRAME_WIDTH * 2);
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

static RecvResult receive_dirty_rects(int sock, uint16_t* target,
                                       uint8_t rect_count, ColorMode colorMode,
                                       uint16_t rle_escape_color) {
    DirtyRect rects[256];
    if (!recv_exact(sock, rects, rect_count * sizeof(DirtyRect))) {
        ESP_LOGE(TAG, "Failed to receive dirty rects");
        return RecvResult::ERROR;
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
                RecvResult rr = receive_dirty_rects(sock, target,
                                                     fh.rect_count, mode,
                                                     fh.rle_escape_color);
                if (rr != RecvResult::OK) {
                    ESP_LOGE(TAG, "Failed to receive flash dirty rects");
                    return rr;
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

    default:
        ESP_LOGW(TAG, "Unknown message type: 0x%02X", msg_type);
        return RecvResult::ERROR;
    }
}

// ============================================================
// TCP Server (called from network_task)
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

    // Get direct access to the LCD DMA framebuffer via Panel_ST7701_Accessible.
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

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

        ESP_LOGI(TAG, "Client connected from " IPSTR,
                 IP2STR((esp_ip4_addr_t*)&client_addr.sin_addr));

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

        while (true) {
            int64_t t0 = esp_timer_get_time();
            RecvResult result = recv_frame(client_sock, framebuf, current_mode);
            int64_t t1 = esp_timer_get_time();

            if (result == RecvResult::OK) {

                if (current_mode == proto::MODE_FLASH && s_flash_mgr.is_loaded()) {
                    // Flash mode: composite directly into DMA framebuffer
                    if (dma_visible) {
                        s_flash_mgr.tick_and_composite(dma_visible, DISP_STRIDE);
                    } else {
                        s_flash_mgr.tick_and_composite(framebuf);
                    }
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
                    gfx.pushImage(DISP_OVERSCAN_LEFT, 0, FRAME_WIDTH, FRAME_HEIGHT, framebuf);
                }

                int64_t t3 = esp_timer_get_time();
                recv_us_total += (t1 - t0);
                composite_us_total += (t2 - t1);
                blit_us_total += (t3 - t2);
                frame_count++;
            } else if (result == RecvResult::NO_CHANGE) {
                if (current_mode == proto::MODE_FLASH && s_flash_mgr.is_loaded()) {
                    // Still animate and composite even with no new data
                    if (dma_visible) {
                        s_flash_mgr.tick_and_composite(dma_visible, DISP_STRIDE);
                        display_flush_cache();
                    } else {
                        s_flash_mgr.tick_and_composite(framebuf);
                        gfx.pushImage(DISP_OVERSCAN_LEFT, 0, FRAME_WIDTH, FRAME_HEIGHT, framebuf);
                    }
                }
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

                if (current_mode == proto::MODE_FLASH) {
                    if (!s_flash_mgr.is_loaded()) {
                        ESP_LOGI(TAG, "Flash mode: waiting for data to load...");
                        s_flash_mgr.init(FLASH_CONFIG_FILE);
                    }
                }
            } else if (result == RecvResult::RESET_REQUESTED) {
                esp_restart();
                break;
            } else if (result == RecvResult::RECONNECT_REQUESTED) {
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
                ESP_LOGI(TAG, "FPS: %.1f | recv: %.1fms | composite: %.1fms | blit: %.1fms | %.1f Mbps (%d frames)",
                         fps, avg_recv_ms, avg_composite_ms, avg_blit_ms, throughput_mbps, frame_count);
                frame_count = 0;
                recv_us_total = 0;
                composite_us_total = 0;
                blit_us_total = 0;
                fps_start = now;
            }
        }

        close(client_sock);
        ESP_LOGI(TAG, "Client disconnected");

        // Show black screen when idle
        if (dma_fb) {
            clear_display(dma_fb);
        } else {
            gfx.fillScreen(0);
        }
    }
}