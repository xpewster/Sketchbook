#pragma once

#include <cstring>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "protocol.h"

// ============================================================
// Pin Configuration — Adafruit Qualia ESP32-S3
// ============================================================
// I2C for IO expander
constexpr int PIN_I2C_SCL = 18;
constexpr int PIN_I2C_SDA = 8;

// IO Expander (TCA9554) for bit-banging SPI to ST7701S
constexpr uint8_t IOEXP_I2C_ADDR   = 0x3F;
constexpr uint8_t IOEXP_REG_OUTPUT = 0x01;
constexpr uint8_t IOEXP_REG_CONFIG = 0x03;
constexpr uint8_t IOEXP_PIN_RESET  = 0x04;  // bit 2
constexpr uint8_t IOEXP_PIN_CS     = 0x02;  // bit 1
constexpr uint8_t IOEXP_PIN_SCK    = 0x01;  // bit 0
constexpr uint8_t IOEXP_PIN_MOSI   = 0x80;  // bit 7

// ============================================================
// Display Configuration - HD371001C40
// ============================================================
constexpr uint32_t DISP_FREQUENCY = 16000000;
constexpr uint16_t DISP_WIDTH = 240;
constexpr uint16_t DISP_HEIGHT = 960;
constexpr uint16_t DISP_OVERSCAN_LEFT = 120;
constexpr int8_t DISP_HSYNC_PULSE_WIDTH = 8;
constexpr int8_t DISP_HSYNC_FRONT_PORCH = 20;
constexpr int8_t DISP_HSYNC_BACK_PORCH = 20;
constexpr bool DISP_HSYNC_IDLE_LOW = false;
constexpr int8_t DISP_VSYNC_PULSE_WIDTH = 8;
constexpr int8_t DISP_VSYNC_FRONT_PORCH = 20;
constexpr int8_t DISP_VSYNC_BACK_PORCH = 20;
constexpr bool DISP_VSYNC_IDLE_LOW = false;
constexpr bool DISP_PCLK_ACTIVE_HIGH = true;
constexpr bool DISP_PCLK_IDLE_HIGH = false;
constexpr bool DISP_DE_IDLE_HIGH = false;

constexpr uint16_t DISP_STRIDE = DISP_WIDTH + DISP_OVERSCAN_LEFT;  // 360 pixels per row in FB

// ============================================================
// Panel subclass — exposes the DMA framebuffer pointer
// ============================================================
// Panel_RGB::_frame_buffer is protected. Rather than patching LovyanGFX,
// we subclass Panel_ST7701 and add an accessor.

struct Panel_ST7701_Accessible : public lgfx::Panel_ST7701 {
    uint8_t* getFrameBuffer() const { return _frame_buffer; }
};

// ============================================================
// LGFX Device Class
// ============================================================

class LGFX : public lgfx::LGFX_Device {
    Panel_ST7701_Accessible _panel;
    lgfx::Bus_RGB           _bus;

public:
    LGFX() {
        // --- Panel config ---
        // LovyanGFX's impl of ST7701 does not use the offset_x properly, so we
        // include the overscan in the panel width and handle it in our drawing code.
        {
            auto cfg = _panel.config();
            cfg.memory_width  = DISP_WIDTH + DISP_OVERSCAN_LEFT;  // 360: overscan + visible
            cfg.memory_height = DISP_HEIGHT;
            cfg.panel_width   = DISP_WIDTH + DISP_OVERSCAN_LEFT;
            cfg.panel_height  = DISP_HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel.config(cfg);
        }

        // --- Panel detail config ---
        // SPI pins set to -1: we handle ST7701S init manually via IO expander.
        // Panel_ST7701 may still do internal setup during gfx.init().
        {
            auto cfg = _panel.config_detail();
            cfg.use_psram = 1;
            cfg.pin_cs   = -1;
            cfg.pin_sclk = -1;
            cfg.pin_mosi = -1;
            _panel.config_detail(cfg);
        }

        // --- RGB Bus config ---
        {
            auto cfg = _bus.config();
            cfg.panel = &_panel;

            // Pin mapping from Qualia ESP32-S3 board definition:
            // TFT_B1=GPIO40, B2=39, B3=38, B4=0, B5=45
            // TFT_G0=GPIO48, G1=47, G2=21, G3=14, G4=13, G5=12
            // TFT_R1=GPIO11, R2=10, R3=9, R4=46, R5=3
            //
            // LovyanGFX d0=bit0(LSB) through d15=bit15(MSB) of RGB565 word:
            //   bits[4:0]=Blue, bits[10:5]=Green, bits[15:11]=Red

            // Blue channel (5 bits): d0=B1(LSB) → d4=B5(MSB)
            cfg.pin_d0  = GPIO_NUM_40;  // B1 (Blue LSB)
            cfg.pin_d1  = GPIO_NUM_39;  // B2
            cfg.pin_d2  = GPIO_NUM_38;  // B3
            cfg.pin_d3  = GPIO_NUM_0;   // B4
            cfg.pin_d4  = GPIO_NUM_45;  // B5 (Blue MSB)

            // Green channel (6 bits): d5=G0(LSB) → d10=G5(MSB)
            cfg.pin_d5  = GPIO_NUM_48;  // G0 (Green LSB)
            cfg.pin_d6  = GPIO_NUM_47;  // G1
            cfg.pin_d7  = GPIO_NUM_21;  // G2
            cfg.pin_d8  = GPIO_NUM_14;  // G3
            cfg.pin_d9  = GPIO_NUM_13;  // G4
            cfg.pin_d10 = GPIO_NUM_12;  // G5 (Green MSB)

            // Red channel (5 bits): d11=R1(LSB) → d15=R5(MSB)
            cfg.pin_d11 = GPIO_NUM_11;  // R1 (Red LSB)
            cfg.pin_d12 = GPIO_NUM_10;  // R2
            cfg.pin_d13 = GPIO_NUM_9;   // R3
            cfg.pin_d14 = GPIO_NUM_46;  // R4
            cfg.pin_d15 = GPIO_NUM_3;   // R5 (Red MSB)

            // Sync/clock pins
            cfg.pin_henable = GPIO_NUM_2;   // DE
            cfg.pin_vsync   = GPIO_NUM_42;  // VSYNC
            cfg.pin_hsync   = GPIO_NUM_41;  // HSYNC
            cfg.pin_pclk    = GPIO_NUM_1;   // Pixel clock

            // Timing — from working CircuitPython config
            cfg.freq_write = DISP_FREQUENCY;

            cfg.hsync_polarity    = DISP_HSYNC_IDLE_LOW;
            cfg.hsync_front_porch = DISP_HSYNC_FRONT_PORCH;
            cfg.hsync_pulse_width = DISP_HSYNC_PULSE_WIDTH;
            cfg.hsync_back_porch  = DISP_HSYNC_BACK_PORCH;

            cfg.vsync_polarity    = DISP_VSYNC_IDLE_LOW;
            cfg.vsync_front_porch = DISP_VSYNC_FRONT_PORCH;
            cfg.vsync_pulse_width = DISP_VSYNC_PULSE_WIDTH;
            cfg.vsync_back_porch  = DISP_VSYNC_BACK_PORCH;

            cfg.pclk_active_neg = !DISP_PCLK_ACTIVE_HIGH;
            cfg.de_idle_high    = DISP_DE_IDLE_HIGH;
            cfg.pclk_idle_high  = DISP_PCLK_IDLE_HIGH;

            _bus.config(cfg);
        }

        _panel.setBus(&_bus);
        setPanel(&_panel);
    }

    // ============================================================
    // Direct DMA framebuffer access
    // ============================================================
    // Bypasses pushImage (which memcpys 450KB every frame) and lets us
    // composite directly into the buffer the LCD DMA reads from,
    // matching what CircuitPython does with esp_lcd_rgb_panel_get_frame_buffer.

    /// Raw DMA framebuffer (full stride including overscan). Returns nullptr before init().
    uint16_t* getFrameBuffer() {
        return (uint16_t*)_panel.getFrameBuffer();
    }

    /// Pointer to start of visible area (offset past left overscan).
    /// Write FRAME_WIDTH pixels per row, advancing by DISP_STRIDE between rows.
    uint16_t* getVisibleBuffer() {
        uint16_t* fb = getFrameBuffer();
        return fb ? fb + DISP_OVERSCAN_LEFT : nullptr;
    }
};

// Global display instance
extern LGFX gfx;

// Initialize ST7701S via IO expander (must be called BEFORE gfx.init()).
// Sends the init sequence over bit-banged SPI through the TCA9554,
// then releases I2C so GPIO 47/48 can be used for RGB data.
bool init_st7701s();

// ============================================================
// Cache writeback — flush CPU cache so LCD DMA sees latest pixels
// ============================================================
// Same approach as CircuitPython's dotclockframebuffer refresh().
// The LCD_CAM DMA reads from PSRAM, but the CPU writes go through
// cache. Without explicit writeback, DMA may read stale data.

extern "C" int Cache_WriteBack_Addr(uint32_t addr, uint32_t size);

inline void display_flush_cache() {
    uint16_t* fb = gfx.getFrameBuffer();
    if (fb) {
        Cache_WriteBack_Addr((uint32_t)fb, DISP_STRIDE * DISP_HEIGHT * sizeof(uint16_t));
    }
}