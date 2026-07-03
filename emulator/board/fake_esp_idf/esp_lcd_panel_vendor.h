#pragma once
// Fake ESP-IDF esp_lcd type definitions — just enough for amoled.h /
// low_level_amoled.h to parse. The real esp_lcd_panel_* functions are never
// called: amoled.cpp (the real implementation) isn't compiled in the
// emulator; amoled_sim.cpp implements Amoled's public methods directly
// against the SDL canvas instead.
#include <cstdint>
#include <cstddef>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef struct esp_lcd_panel_t*    esp_lcd_panel_handle_t;
typedef struct esp_lcd_panel_io_t* esp_lcd_panel_io_handle_t;

typedef struct {
    int reset_gpio_num;
    int rgb_ele_order;
    int bits_per_pixel;
    void* vendor_config;
} esp_lcd_panel_dev_config_t;
