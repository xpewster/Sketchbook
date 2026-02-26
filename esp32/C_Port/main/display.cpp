#include "display.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "display";

// Global display instance
LGFX gfx;

// I2C handles
static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static i2c_master_dev_handle_t s_i2c_dev = nullptr;

// ============================================================
// ST7701S Init Sequence
// ============================================================
// Format: [cmd, len_flags, data..., delay?]
//   len_flags bit 7: has delay byte after data
//   len_flags bits 0-6: number of data bytes

static const uint8_t st7701s_init_sequence[] = {
    0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x13,
    0xEF, 0x01, 0x08,
    0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x10,
    0xC0, 0x02, 0x77, 0x00,
    // RGBCTRL: sync polarity to match Bus_RGB config
    0xC3, 0x03, 0x0C, 0x10, 0x08,
    0xC1, 0x02, 0x11, 0x0C,
    0xC2, 0x02, 0x07, 0x02,
    0xCC, 0x01, 0x30,
    0xB0, 0x10, 0x06, 0xCF, 0x14, 0x0C, 0x0F, 0x03, 0x00, 0x0A, 0x07, 0x1B, 0x03, 0x12, 0x10, 0x25, 0x36, 0x1E,
    0xB1, 0x10, 0x0C, 0xD4, 0x18, 0x0C, 0x0E, 0x06, 0x03, 0x06, 0x08, 0x23, 0x06, 0x12, 0x10, 0x30, 0x2F, 0x1F,
    0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x11,
    0xB0, 0x01, 0x73,
    0xB1, 0x01, 0x7C,
    0xB2, 0x01, 0x83,
    0xB3, 0x01, 0x80,
    0xB5, 0x01, 0x49,
    0xB7, 0x01, 0x87,
    0xB8, 0x01, 0x33,
    0xB9, 0x02, 0x10, 0x1F,
    0xBB, 0x01, 0x03,
    0xC1, 0x01, 0x08,
    0xC2, 0x01, 0x08,
    0xD0, 0x01, 0x88,
    0xE0, 0x06, 0x00, 0x00, 0x02, 0x00, 0x00, 0x0C,
    0xE1, 0x0B, 0x05, 0x96, 0x07, 0x96, 0x06, 0x96, 0x08, 0x96, 0x00, 0x44, 0x44,
    0xE2, 0x0C, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0xE3, 0x04, 0x00, 0x00, 0x33, 0x33,
    0xE4, 0x02, 0x44, 0x44,
    0xE5, 0x10, 0x0D, 0xD4, 0x28, 0x8C, 0x0F, 0xD6, 0x28, 0x8C, 0x09, 0xD0, 0x28, 0x8C, 0x0B, 0xD2, 0x28, 0x8C,
    0xE6, 0x04, 0x00, 0x00, 0x33, 0x33,
    0xE7, 0x02, 0x44, 0x44,
    0xE8, 0x10, 0x0E, 0xD5, 0x28, 0x8C, 0x10, 0xD7, 0x28, 0x8C, 0x0A, 0xD1, 0x28, 0x8C, 0x0C, 0xD3, 0x28, 0x8C,
    0xEB, 0x06, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x00,
    0xED, 0x10, 0xF3, 0xC1, 0xBA, 0x0F, 0x66, 0x77, 0x44, 0x55, 0x55, 0x44, 0x77, 0x66, 0xF0, 0xAB, 0x1C, 0x3F,
    0xEF, 0x06, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F,
    0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x13,
    0xE8, 0x02, 0x00, 0x0E,
    0x11, 0x80, 0x78,                     // Sleep Out + 120ms delay
    0xE8, 0x82, 0x00, 0x0C, 0x0A,        // + 10ms delay
    0xE8, 0x02, 0x40, 0x00,
    0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x00,
    0x36, 0x01, 0x00,
    0x3A, 0x01, 0x66,                     // COLMOD: RGB565
    0x29, 0x80, 0x14,                     // Display On + 20ms delay
    0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x10,
    0xE5, 0x02, 0x00, 0x00,
};

// ============================================================
// IO Expander I2C Helpers
// ============================================================

static esp_err_t ioexp_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_i2c_dev, buf, 2, pdMS_TO_TICKS(10));
}

// ============================================================
// IO Expander SPI Bit-Bang
// ============================================================

// Base output state: must match CircuitPython's gpio_data=0xFD behavior.
// gpio_data keeps ALL bits high except CS (bit 1) during SPI.
// Bits 3,4,5,6 are other IO expander outputs (bit 4 = backlight enable).
// We must keep them HIGH at all times during SPI communication.
// Bit 0 (CLK) and bit 7 (MOSI) are toggled per-bit, bit 1 (CS) is toggled per-command.
static constexpr uint8_t SPI_BASE = IOEXP_PIN_RESET | 0x78;  // 0x04 | 0x78 = 0x7C (bits 2,3,4,5,6)

static void spi_write_9bit(uint8_t dc, uint8_t data) {
    uint16_t word = ((dc ? 1 : 0) << 8) | data;

    for (int bit = 8; bit >= 0; bit--) {
        uint8_t pins = SPI_BASE;  // Always keep RESET high
        if (word & (1 << bit)) pins |= IOEXP_PIN_MOSI;
        // CS is low (not set), CLK is low (not set yet)

        ioexp_write(IOEXP_REG_OUTPUT, pins);                  // data out, CLK low
        ioexp_write(IOEXP_REG_OUTPUT, pins | IOEXP_PIN_SCK);  // CLK high (rising edge latches)
    }
    // Idle: CLK low, CS still low, RESET high
    ioexp_write(IOEXP_REG_OUTPUT, SPI_BASE);
}

static void spi_send_command(uint8_t cmd, const uint8_t* data, int len) {
    // CS low (RESET stays high)
    ioexp_write(IOEXP_REG_OUTPUT, SPI_BASE);
    spi_write_9bit(0, cmd);                   // DC=0 for command
    for (int i = 0; i < len; i++) {
        spi_write_9bit(1, data[i]);           // DC=1 for data
    }
    // CS high, CLK idle, RESET high
    ioexp_write(IOEXP_REG_OUTPUT, SPI_BASE | IOEXP_PIN_CS);
}

static void send_init_sequence(const uint8_t* seq, size_t len) {
    size_t pos = 0;
    int cmd_count = 0;

    while (pos < len) {
        uint8_t cmd = seq[pos++];
        if (pos >= len) break;

        uint8_t len_flags = seq[pos++];
        int data_len = len_flags & 0x7F;
        bool has_delay = (len_flags & 0x80) != 0;

        const uint8_t* data = &seq[pos];
        pos += data_len;

        spi_send_command(cmd, data, data_len);
        cmd_count++;

        if (has_delay && pos < len) {
            int delay_ms = seq[pos++];
            if (delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
        }
    }
    ESP_LOGI(TAG, "Sent %d ST7701S init commands", cmd_count);
}

// ============================================================
// Public: init_st7701s()
// ============================================================

bool init_st7701s() {
    // Set up I2C bus on GPIO 8 (SDA) / GPIO 18 (SCL)
    // These are dedicated I2C pins, NOT shared with the RGB bus.
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = (gpio_num_t)PIN_I2C_SDA;
    bus_cfg.scl_io_num = (gpio_num_t)PIN_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = IOEXP_I2C_ADDR;
    dev_cfg.scl_speed_hz = 400000;

    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(err));
        return false;
    }

    // Verify IO expander is present
    err = i2c_master_probe(s_i2c_bus, IOEXP_I2C_ADDR, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IO expander not found at 0x%02X: %s", IOEXP_I2C_ADDR, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "IO expander found at 0x%02X (SDA=%d, SCL=%d)", IOEXP_I2C_ADDR, PIN_I2C_SDA, PIN_I2C_SCL);

    // Configure IO expander: pins 0,1,2,7 as outputs, rest inputs
    // Matches CircuitPython i2c_init_sequence: b'\x02\x03\x78\x02\x02\x00'
    uint8_t out_mask = IOEXP_PIN_RESET | IOEXP_PIN_CS | IOEXP_PIN_SCK | IOEXP_PIN_MOSI;
    ioexp_write(IOEXP_REG_CONFIG, ~out_mask & 0xFF);  // 0x03 = 0x78
    ioexp_write(0x02, 0x00);  // Polarity: no inversion

    // Hardware reset: pull RESET low, keep CS high and spare pins high
    // Matches CircuitPython gpio_data=0xFD with reset_mask cleared
    ioexp_write(IOEXP_REG_OUTPUT, IOEXP_PIN_CS | 0x78);  // CS high, bits 3-6 high, RESET low
    vTaskDelay(pdMS_TO_TICKS(10));
    ioexp_write(IOEXP_REG_OUTPUT, IOEXP_PIN_CS | IOEXP_PIN_RESET | 0x78);  // Release reset
    vTaskDelay(pdMS_TO_TICKS(100));

    // Send init sequence
    ESP_LOGI(TAG, "Sending ST7701S init sequence...");
    send_init_sequence(st7701s_init_sequence, sizeof(st7701s_init_sequence));

    // Note: I2C bus stays up — GPIO 8/18 are not used by the RGB bus
    ESP_LOGI(TAG, "ST7701S initialized");
    return true;
}