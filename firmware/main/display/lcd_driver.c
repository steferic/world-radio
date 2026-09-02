#include "lcd_driver.h"
#include "config.h"

#include <string.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd_driver";
static spi_device_handle_t s_spi;

// ST7789 command set (only what's needed for init + writing pixels).
#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_COLMOD  0x3A
#define CMD_MADCTL  0x36
#define CMD_INVON   0x21
#define CMD_NORON   0x13
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C

static inline void lcd_set_dc(int level)
{
    gpio_set_level(LCD_DC_GPIO, level);
}

static void lcd_send_cmd(uint8_t cmd)
{
    lcd_set_dc(0); // command mode
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    lcd_set_dc(1); // data mode
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static inline void lcd_send_data_byte(uint8_t data)
{
    lcd_send_data(&data, 1);
}

// Sets the rectangular window the next RAMWR will fill, inclusive on both
// ends (x1/y1 are the last column/row, not one-past-the-end).
static void lcd_set_addr_window(int x0, int y0, int x1, int y1)
{
    uint8_t caset[4] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF), (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
    uint8_t raset[4] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF), (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };

    lcd_send_cmd(CMD_CASET);
    lcd_send_data(caset, sizeof(caset));
    lcd_send_cmd(CMD_RASET);
    lcd_send_data(raset, sizeof(raset));
    lcd_send_cmd(CMD_RAMWR);
}

esp_err_t lcd_driver_init(void)
{
    gpio_config_t dc_cfg = {
        .pin_bit_mask = 1ULL << LCD_DC_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&dc_cfg);

    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << LCD_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_SCK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1, // display is write-only from our side
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32768,
    };
    esp_err_t err = spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = LCD_CS_GPIO,
        .queue_size = 4,
    };
    err = spi_bus_add_device(LCD_SPI_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    // Hardware reset.
    gpio_set_level(LCD_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_send_cmd(CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_send_cmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_send_cmd(CMD_COLMOD);
    lcd_send_data_byte(0x55); // 16 bits/pixel, RGB565

    lcd_send_cmd(CMD_MADCTL);
    lcd_send_data_byte(LCD_MADCTL); // rotation/mirroring -- see config.h if the image is wrong

    // Most ST7789 panels need this to show colors correctly -- if a white
    // background renders as black (or colors otherwise look inverted),
    // that's this setting, not a wiring problem. Remove it if your panel
    // already looks right without it.
    lcd_send_cmd(CMD_INVON);

    lcd_send_cmd(CMD_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_send_cmd(CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BLACK);

    ESP_LOGI(TAG, "ST7789 ready (%dx%d)", LCD_WIDTH, LCD_HEIGHT);
    return ESP_OK;
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) {
        return;
    }

    lcd_set_addr_window(x, y, x + w - 1, y + h - 1);

    // One row-sized static buffer, sent once per row, instead of
    // allocating a full w*h buffer -- keeps a full-screen clear cheap on
    // both stack and heap.
    static uint16_t line_buf[LCD_WIDTH];
    uint16_t swapped = (uint16_t)((color << 8) | (color >> 8)); // ST7789 wants big-endian RGB565 over SPI
    for (int i = 0; i < w; i++) {
        line_buf[i] = swapped;
    }

    lcd_set_dc(1);
    for (int row = 0; row < h; row++) {
        spi_transaction_t t = {
            .length = (size_t)w * 16,
            .tx_buffer = line_buf,
        };
        spi_device_polling_transmit(s_spi, &t);
    }
}

void lcd_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x + w > LCD_WIDTH || y + h > LCD_HEIGHT) {
        ESP_LOGW(TAG, "lcd_draw_bitmap: (%d,%d) %dx%d is out of bounds, skipping", x, y, w, h);
        return;
    }

    lcd_set_addr_window(x, y, x + w - 1, y + h - 1);

    // Byte-swap into a scratch buffer -- the caller's buffer is in the
    // ESP32's native (little-endian) order, but ST7789 wants big-endian
    // RGB565 over the wire.
    static uint16_t swap_buf[LCD_WIDTH];
    lcd_set_dc(1);
    for (int row = 0; row < h; row++) {
        const uint16_t *src = pixels + (size_t)row * w;
        for (int i = 0; i < w; i++) {
            swap_buf[i] = (uint16_t)((src[i] << 8) | (src[i] >> 8));
        }
        spi_transaction_t t = {
            .length = (size_t)w * 16,
            .tx_buffer = swap_buf,
        };
        spi_device_polling_transmit(s_spi, &t);
    }
}
