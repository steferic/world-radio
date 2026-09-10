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

// Commands shared by ST7789 and ILI9341 (both use the same opcodes and
// argument formats for these -- CASET/RASET/RAMWR and the basic control
// commands are common to nearly every MIPI DBI-compatible TFT controller).
#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_COLMOD  0x3A
#define CMD_MADCTL  0x36
#define CMD_INVON   0x21  // sent on both chips -- see lcd_driver_init for why
#define CMD_NORON   0x13
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C

// ILI9341 extended-init commands (power, VCOM, frame rate, gamma). ST7789
// works fine off its reset defaults; ILI9341 is more contrast/stability
// sensitive, so we run the vendor-recommended sequence on that chip.
#define CMD_PWCTRL1  0xC0
#define CMD_PWCTRL2  0xC1
#define CMD_VMCTRL1  0xC5
#define CMD_VMCTRL2  0xC7
#define CMD_FRMCTR1  0xB1
#define CMD_DFUNCTR  0xB6
#define CMD_GAMSET   0x26
#define CMD_PGAMMA   0xE0
#define CMD_NGAMMA   0xE1

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

// Sets the rectangular window the next RAMWR will fill, inclusive on both ends.
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
    lcd_send_data_byte(0x55); // 16 bits/pixel, RGB565 -- same on both chips

    lcd_send_cmd(CMD_MADCTL);
    lcd_send_data_byte(LCD_MADCTL); // rotation/mirroring. Alter config.h if the image is wrong.

    // INVON is sent unconditionally, on BOTH chips. The rest of the
    // firmware's color constants (see LCD_COLOR_* in lcd_driver.h) were
    // authored against an ST7789 with pixel inversion on -- values in
    // GRAM are the complement of what appears on screen. Skipping INVON
    // on ILI9341 would render every color as its inverse (whites black,
    // blacks white, yellows blue, etc.) even though "raw" ILI9341
    // panels don't need it.
    lcd_send_cmd(CMD_INVON);

#if LCD_CONTROLLER == LCD_CONTROLLER_ST7789
    const char *chip_name = "ST7789";
#elif LCD_CONTROLLER == LCD_CONTROLLER_ILI9341
    // Vendor-recommended power/VCOM/frame-rate/gamma init. ILI9341 will
    // run off its reset defaults, but contrast and stability are markedly
    // better with these values set. Numbers pulled from Adafruit's
    // reference library for the 2.8" TFT (product 1770).
    lcd_send_cmd(CMD_PWCTRL1);
    lcd_send_data_byte(0x23);              // VRH = 4.60 V
    lcd_send_cmd(CMD_PWCTRL2);
    lcd_send_data_byte(0x10);              // BT = AVDD/VGH/VGL selection
    uint8_t vmctrl1[] = { 0x3E, 0x28 };
    lcd_send_cmd(CMD_VMCTRL1);
    lcd_send_data(vmctrl1, sizeof(vmctrl1));
    lcd_send_cmd(CMD_VMCTRL2);
    lcd_send_data_byte(0x86);
    uint8_t frmctr1[] = { 0x00, 0x18 };    // ~79 Hz
    lcd_send_cmd(CMD_FRMCTR1);
    lcd_send_data(frmctr1, sizeof(frmctr1));
    uint8_t dfunctr[] = { 0x08, 0x82, 0x27 };
    lcd_send_cmd(CMD_DFUNCTR);
    lcd_send_data(dfunctr, sizeof(dfunctr));
    lcd_send_cmd(CMD_GAMSET);
    lcd_send_data_byte(0x01);              // curve 1
    uint8_t pgamma[] = { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                         0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 };
    lcd_send_cmd(CMD_PGAMMA);
    lcd_send_data(pgamma, sizeof(pgamma));
    uint8_t ngamma[] = { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                         0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F };
    lcd_send_cmd(CMD_NGAMMA);
    lcd_send_data(ngamma, sizeof(ngamma));
    const char *chip_name = "ILI9341";
#else
    #error "Unknown LCD_CONTROLLER value -- see config.h"
#endif

    lcd_send_cmd(CMD_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_send_cmd(CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    // DEBUGGING
    vTaskDelay(pdMS_TO_TICKS(1000));
    lcd_send_cmd(0x28);  // DISPOFF -- valid on both chips
    vTaskDelay(pdMS_TO_TICKS(2000));
    lcd_send_cmd(0x29);  // DISPON

    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BLACK);

    ESP_LOGI(TAG, "%s ready (%dx%d)", chip_name, LCD_WIDTH, LCD_HEIGHT);
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

    // Instead of allocating a large w*h-sized 2D buffer, we save on stack
    // and heap size by sending one row-length buffer, for as many rows as
    // we need, one after another.
    static uint16_t line_buf[LCD_WIDTH];
    uint16_t swapped = (uint16_t)((color << 8) | (color >> 8)); // panel wants big-endian RGB565 over SPI
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

    // Byte-swap into a scratch buffer. The caller's 'pixels' buffer is in the
    // ESP32's native little-endian order, but the panel wants big-endian
    // RGB565 (true for both ST7789 and ILI9341).
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
