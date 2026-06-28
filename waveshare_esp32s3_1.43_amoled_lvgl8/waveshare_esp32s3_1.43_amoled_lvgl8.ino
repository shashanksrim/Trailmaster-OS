/**
 * PROJECT: JIMNY DASH V3 + NES Games (ULTIMATE UNIFIED EDITION)
 */

#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>
#include <math.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <AnimatedGIF.h>
#include "amoled.h"   
#include "FT3168.h"   
#include "ui.h"       
#include "qmi8658c.h" 
#include "screen_ui.h"
#include "ui_godzillaspeedometer.h"
#include "screen_game.h"
#include "retro_engine.h"
#include "screen_inclinometer.h"
#include "PhotoFrameApp.h"
#include "NesEngine.h"
#include "sd_card_bsp.h"
#include "OTAManager.h"
#include "obd_parse.h"   // pure OBD-II PID parsers (also unit-tested on host)
#include <dirent.h>
#include <unistd.h>
#include <FFat.h>

extern Amoled amoled; 

// Persistent Reboot Flag (Survives ESP.restart)
RTC_DATA_ATTR int reboot_into_games = 0;
extern "C" void set_reboot_games(int val) { reboot_into_games = val; }

// LVGL Buffers (BALANCED PSRAM ALLOCATION)
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
#define SCREEN_WIDTH  466
#define SCREEN_HEIGHT 466
#define LVGL_DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT) 
lv_color_t *lvgl_buf1 = nullptr;
lv_color_t *lvgl_buf2 = nullptr;

extern "C" {
    lv_img_dsc_t * dsc_godzilla = nullptr;
}

// Global reference for PhotoFrameApp overlay
extern lv_obj_t* ui_Panel2;

lv_color_t get_dynamic_color(float percent) {
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    if (percent < 0.40f) return lv_color_make(0, 150, 255); 
    if (percent < 0.80f) return lv_color_make(0, 255, 0);   
    return lv_color_make(255, 0, 0);                        
}

// Device State
enum DeviceMode { MODE_UI, MODE_GAME, MODE_INCLINOMETER, MODE_PHOTOFRAME, MODE_EMULATOR, MODE_NES_BROWSER };
volatile DeviceMode currentMode = MODE_UI;

// --- NES BROWSER STATE ---
static lv_obj_t * ui_nes_screen = NULL;
static lv_obj_t * rom_list = NULL;
static char selected_rom_path[128] = "";
static bool game_selected = false;
extern "C" const lv_font_t ui_font_rajdhani1;
extern "C" const lv_font_t ui_font_rajdhani;

// --- SETTINGS STATE ---
static lv_obj_t * settings_screen = NULL;
#include <Preferences.h>
#include "grid_icons.h"

static int brightness_level = 8; // 1-10, default 8 (~200/255)
int use_grid_launcher = 1; // 1 = grid, 0 = list
int use_jimny_logo = 0; // 1 = jimny logo, 0 = custom/trailmaster (Default OFF as requested)
lv_obj_t * sel_bar = NULL;
lv_obj_t * grid_container = NULL;

// --- JIMNY DASHBOARD STATE ---
bool imu_ready = false;
float raw_imu_pitch = 0.0, raw_imu_roll = 0.0;    // filtered IMU angles (degrees)
float imu_pitch_offset = 0.0, imu_roll_offset = 0.0; // zeroing offsets captured at reset
unsigned long last_imu_time = 0;
int imu_settle_count = 0; // global for IMU damping on start
volatile int car_engine_temp = 0, car_engine_load = 0, car_rpm = 0, car_speed = 0;
volatile float car_voltage = 13.8; 
uint32_t screen_load_time = 0;
uint16_t touch_start_y = 0;
#define JIMNY_FW_VERSION "Trailmaster v2.3.24"
bool auto_reset_triggered = false; // tracks 1s auto-calibration state
uint32_t last_restart_time = 0; 
extern float jump_vel;
lv_obj_t * last_screen_before_launcher = NULL;

extern "C" {
    #include "ui.h"
    void build_rom_menu();
    void build_settings_screen();
    void build_about_screen();
    void app_start_dino_game(lv_event_t * e);
    void app_open_games_menu(lv_event_t * e);
    void load_ui_mode(lv_event_t * e);
    void stop_dino_game(lv_event_t * e);
    void app_dino_jump_trigger(lv_event_t * e);
    void load_inclinometer_mode(lv_event_t * e);
    void app_zero_inclinometer(lv_event_t * e);
    void app_imageframe(lv_event_t * e);
    void app_settings(lv_event_t * e);
    void app_about(lv_event_t * e);
}

void switch_to_launcher();
void force_full_ui_redraw(lv_obj_t * target_scr);
void build_grid_launcher();
extern void pf_show_upload_overlay(void);
extern bool pf_autostart_wifi;   // defined in PhotoFrameApp.cpp

// --- SHARED UI LOGIC ---
bool ignore_until_lift = false;

// --- OBD Wi-Fi Connectivity Configuration ---
const char* obd_ssid = "WiFi_OBDII"; 
const char* obd_ip = "192.168.0.10";
const uint16_t obd_port = 35000;

void read_obd_response(WiFiClient& client, char* buffer, size_t max_len) {
    unsigned long timeout = millis() + 1500; 
    int len = 0;
    while (millis() < timeout) {
        if (client.available()) {
            char c = client.read();
            if (c == '\r' || c == '\n') continue; 
            if (c == '>') { buffer[len] = '\0'; break; }
            if (len < max_len - 1) buffer[len++] = c;
        }
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
    buffer[len] = '\0';
}

void obdBackgroundWorker(void *pvParameters) {
    WiFiClient client;
    char rx_buf[64];
    uint32_t loop_counter = 0;
    
    extern bool wifi_ap_running;
    while(1) {
        // Yield if we are in other heavy modes, if AP is running, or if OTA is active to prevent resource/WiFi conflicts
        const OTAStatus* ota_st = ota_get_status();
        if (currentMode == MODE_PHOTOFRAME || currentMode == MODE_EMULATOR || wifi_ap_running || ota_st->state != OTA_IDLE) { 
            if (client.connected()) { client.stop(); }
            if (!wifi_ap_running && ota_st->state == OTA_IDLE) {
                WiFi.disconnect(true, true);
            }
            vTaskDelay(pdMS_TO_TICKS(1000)); 
            continue; 
        }
        
        // Ensure WiFi Connection in Station Mode
        if (WiFi.status() != WL_CONNECTED) {
            // Check one more time before committing to a 10s blocking WiFi attempt
            if (ota_st->state != OTA_IDLE) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            Serial.println("[OBD] Connecting to WiFi_OBDII...");
            WiFi.disconnect(true, true);
            vTaskDelay(pdMS_TO_TICKS(100));
            WiFi.mode(WIFI_STA);
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
            IPAddress local_IP(192, 168, 0, 11);
            IPAddress gateway(192, 168, 0, 10);
            IPAddress subnet(255, 255, 255, 0);
            WiFi.config(local_IP, gateway, subnet);
            WiFi.begin(obd_ssid);
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
                if (ota_get_status()->state != OTA_IDLE) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(500)); 
                attempts++; 
            }
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[OBD] WiFi Connection Failed!");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue; 
            }
            Serial.println("[OBD] WiFi Connected!");
        }
        
        // Ensure TCP Client Connection to OBD ELM327 Hotspot
        if (!client.connected()) {
            Serial.println("[OBD] Connecting to ELM327 TCP port...");
            if (client.connect(obd_ip, obd_port)) {
                Serial.println("[OBD] ELM327 TCP Port Connected! Initializing adapter...");
                client.print("ATZ\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
                vTaskDelay(pdMS_TO_TICKS(1000));
                client.print("ATE E0\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
                vTaskDelay(pdMS_TO_TICKS(100));
            } else { 
                Serial.println("[OBD] Connection to ELM327 Failed!");
                vTaskDelay(pdMS_TO_TICKS(1000)); 
                continue; 
            }
        }
        
        // --- DYNAMIC SCREEN-BASED SELECTIVE POLL OBD OPTIMIZER ---
        bool is_gauge_active = (ui_uigauge != NULL && lv_scr_act() == ui_uigauge);

        if (is_gauge_active) {
            // Read Coolant Temperature (PID 0105)
            client.print("0105\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
            obd_parse_coolant(rx_buf, (int*)&car_engine_temp);
            vTaskDelay(pdMS_TO_TICKS(10));

            // Read Engine Load (PID 0104)
            client.print("0104\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
            obd_parse_load(rx_buf, (int*)&car_engine_load);
            vTaskDelay(pdMS_TO_TICKS(10));

            // Read Battery Voltage (ATRV)
            client.print("ATRV\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
            obd_parse_voltage(rx_buf, (float*)&car_voltage);
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            // Read Engine RPM (PID 010C)
            client.print("010C\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
            obd_parse_rpm(rx_buf, (int*)&car_rpm);
            vTaskDelay(pdMS_TO_TICKS(10));

            // Read Vehicle Speed (PID 010D)
            client.print("010D\r"); read_obd_response(client, rx_buf, sizeof(rx_buf));
            obd_parse_speed(rx_buf, (int*)&car_speed);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        loop_counter++;
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

lv_img_dsc_t* load_bmp_to_psram(const char* path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    
    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54) { fclose(f); return NULL; }
    
    if (header[0] != 'B' || header[1] != 'M') { fclose(f); return NULL; }
    
    uint32_t data_offset = header[10] | (header[11]<<8) | (header[12]<<16) | (header[13]<<24);
    int w = header[18] | (header[19]<<8) | (header[20]<<16) | (header[21]<<24);
    int h_signed = header[22] | (header[23]<<8) | (header[24]<<16) | (header[25]<<24);
    int h = abs(h_signed);
    int bpp = header[28] | (header[29]<<8);
    
    if (bpp != 24 && bpp != 32) { fclose(f); return NULL; }
    
    int row_size = ((w * (bpp/8)) + 3) & ~3;
    uint8_t * row_buf = (uint8_t*)malloc(row_size);
    if (!row_buf) { fclose(f); return NULL; }
    
    uint8_t * img_data = (uint8_t *)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
    if (!img_data) { free(row_buf); fclose(f); return NULL; }
    
    fseek(f, data_offset, SEEK_SET);
    
    bool bottom_up = (h_signed > 0);
    
    for (int y = 0; y < h; y++) {
        int target_y = bottom_up ? (h - 1 - y) : y;
        fread(row_buf, 1, row_size, f);
        uint16_t * dst = (uint16_t*)(img_data + (target_y * w * 2));
        for (int x = 0; x < w; x++) {
            uint8_t b = row_buf[x * (bpp/8) + 0];
            uint8_t g = row_buf[x * (bpp/8) + 1];
            uint8_t r = row_buf[x * (bpp/8) + 2];
            dst[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
    free(row_buf);
    fclose(f);
    
    lv_img_dsc_t * dsc = (lv_img_dsc_t *)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
    if (!dsc) { heap_caps_free(img_data); return NULL; }
    
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
    dsc->header.always_zero = 0;
    dsc->header.reserved = 0;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->data_size = w * h * 2;
    dsc->data = img_data;
    
    return dsc;
}

static lv_obj_t * rom_screen = NULL;

// helper: apply brightness_level (1-10) to display
static void apply_brightness() {
    uint8_t raw = (uint8_t)(25 + (brightness_level - 1) * 25); // 25,50,...,255
    amoled.setBrightness(raw);
    Serial.printf("[SETTINGS] Brightness %d → 0x%02X\n", brightness_level, raw);
}

void build_settings_screen() {
    if (settings_screen == NULL) {
        settings_screen = lv_obj_create(NULL);
    } else {
        lv_obj_clean(settings_screen);
    }
    lv_scr_load_anim(settings_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    currentMode = MODE_UI;

    lv_obj_t * scr = settings_screen;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE); // screen itself doesn't scroll

    // --- Fixed Title ---
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    // --- Scrollable content area below title ---
    lv_obj_t * scroll_cont = lv_obj_create(scr);
    lv_obj_set_size(scroll_cont, 466, 390);
    lv_obj_align(scroll_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(scroll_cont, 0, 0);
    lv_obj_set_style_border_width(scroll_cont, 0, 0);
    lv_obj_set_style_pad_all(scroll_cont, 0, 0);
    lv_obj_set_style_pad_row(scroll_cont, 0, 0);
    lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scroll_cont, 8, 0);
    lv_obj_set_style_pad_bottom(scroll_cont, 20, 0);
    lv_obj_set_style_pad_gap(scroll_cont, 10, 0);

    // Use scroll_cont as parent for all rows
    lv_obj_t * scr_rows = scroll_cont;

    // ─── ROW 1: Brightness ───────────────────────────────────────────────────
    lv_obj_t * row1 = lv_obj_create(scr_rows);
    lv_obj_set_size(row1, 430, 80);
    lv_obj_set_style_bg_color(row1, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row1, 255, 0);
    lv_obj_set_style_border_width(row1, 1, 0);
    lv_obj_set_style_border_color(row1, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row1, 12, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_bright = lv_label_create(row1);
    lv_label_set_text(lbl_bright, "Brightness");
    lv_obj_set_style_text_font(lbl_bright, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_bright, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_bright, LV_ALIGN_LEFT_MID, 14, 0);

    // Container for controls to ensure perfect visual centering
    lv_obj_t * bright_cont = lv_obj_create(row1);
    lv_obj_set_size(bright_cont, 180, 60);
    lv_obj_align(bright_cont, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_opa(bright_cont, 0, 0);
    lv_obj_set_style_border_width(bright_cont, 0, 0);
    lv_obj_set_style_pad_all(bright_cont, 0, 0);
    lv_obj_clear_flag(bright_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(bright_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bright_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // − button
    lv_obj_t * btn_minus = lv_btn_create(bright_cont);
    lv_obj_set_size(btn_minus, 50, 50);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_style_radius(btn_minus, 25, 0);
    lv_obj_t * lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_set_style_text_font(lbl_minus, &ui_font_rajdhani1, 0);
    lv_obj_center(lbl_minus);

    // value label
    lv_obj_t * lbl_val = lv_label_create(bright_cont);
    lv_label_set_text_fmt(lbl_val, "%d", brightness_level);
    lv_obj_set_style_text_font(lbl_val, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0xFF9500), 0);

    // + button
    lv_obj_t * btn_plus = lv_btn_create(bright_cont);
    lv_obj_set_size(btn_plus, 50, 50);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_style_radius(btn_plus, 25, 0);
    lv_obj_t * lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_set_style_text_font(lbl_plus, &ui_font_rajdhani1, 0);
    lv_obj_center(lbl_plus);

    // Wire callbacks
    lv_obj_add_event_cb(btn_minus, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
        if (brightness_level > 1) { brightness_level--; apply_brightness(); lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d", brightness_level); }
    }, LV_EVENT_ALL, lbl_val);

    lv_obj_add_event_cb(btn_plus, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
        if (brightness_level < 10) { brightness_level++; apply_brightness(); lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d", brightness_level); }
    }, LV_EVENT_ALL, lbl_val);

    // ─── ROW 1.4: Wi-Fi settings (opens hotspot/QR overlay) ─────────────────
    lv_obj_t * row_wifi = lv_obj_create(scr_rows);
    lv_obj_set_size(row_wifi, 430, 80);
    lv_obj_set_style_bg_color(row_wifi, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_wifi, 255, 0);
    lv_obj_set_style_border_width(row_wifi, 1, 0);
    lv_obj_set_style_border_color(row_wifi, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_wifi, 12, 0);
    lv_obj_clear_flag(row_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row_wifi, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * lbl_wifi_s = lv_label_create(row_wifi);
    lv_label_set_text(lbl_wifi_s, "Wifi settings");
    lv_obj_set_style_text_font(lbl_wifi_s, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_wifi_s, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_wifi_s, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * arrow_wifi = lv_label_create(row_wifi);
    lv_label_set_text(arrow_wifi, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow_wifi, lv_color_hex(0x888888), 0);
    lv_obj_align(arrow_wifi, LV_ALIGN_RIGHT_MID, -18, 0);

    lv_obj_add_event_cb(row_wifi, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) pf_show_upload_overlay();
    }, LV_EVENT_ALL, NULL);

    // ─── ROW 1.5: Jimny Look Toggle ─────────────────────────────────────────
    lv_obj_t * row_logo = lv_obj_create(scr_rows);
    lv_obj_set_size(row_logo, 430, 80);
    lv_obj_set_style_bg_color(row_logo, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_logo, 255, 0);
    lv_obj_set_style_border_width(row_logo, 1, 0);
    lv_obj_set_style_border_color(row_logo, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_logo, 12, 0);
    lv_obj_clear_flag(row_logo, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_logo = lv_label_create(row_logo);
    lv_label_set_text(lbl_logo, "Jimny mode");
    lv_obj_set_style_text_font(lbl_logo, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_logo, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * sw_logo = lv_switch_create(row_logo);
    lv_obj_align(sw_logo, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_size(sw_logo, 60, 30);
    lv_obj_set_style_bg_color(sw_logo, lv_color_hex(0xFF6A00), LV_PART_INDICATOR | LV_STATE_CHECKED);
    
    if (use_jimny_logo) lv_obj_add_state(sw_logo, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw_logo, LV_STATE_CHECKED);

    lv_obj_add_event_cb(sw_logo, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        lv_obj_t * obj = lv_event_get_target(e);
        use_jimny_logo = lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1 : 0;
        Preferences p;
        p.begin("hellojimny", false);
        p.putInt("jimny_logo", use_jimny_logo);
        p.end();
        Serial.printf("[SETTINGS] Jimny Mode set to %d\n", use_jimny_logo);

        extern lv_obj_t * ui_Image10;
        extern lv_obj_t * ui_Image7;
        extern lv_obj_t * ui_Image8;
        extern lv_obj_t * ui_Image3;
        extern const lv_img_dsc_t ui_img_trailmaster;
        extern const lv_img_dsc_t ui_img_1814113988;
        
        const lv_img_dsc_t* other_logo = use_jimny_logo ? &ui_img_1814113988 : &ui_img_trailmaster;
        
        if (ui_Image10) { lv_img_set_src(ui_Image10, &ui_img_trailmaster); lv_img_set_zoom(ui_Image10, 230); lv_obj_set_x(ui_Image10, 0); } // Launcher is always Trailmaster
        if (ui_Image7) { lv_img_set_src(ui_Image7, other_logo); lv_img_set_zoom(ui_Image7, use_jimny_logo ? 200 : 230); lv_obj_set_x(ui_Image7, 0); }
        if (ui_Image8) { lv_img_set_src(ui_Image8, other_logo); lv_img_set_zoom(ui_Image8, 150); lv_obj_set_y(ui_Image8, -112); }
        if (ui_Image3) { lv_img_set_src(ui_Image3, other_logo); lv_img_set_zoom(ui_Image3, use_jimny_logo ? 200 : 230); lv_obj_set_x(ui_Image3, 0); } // Same as inclinometer
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ─── ROW 1.75: Grid Launcher Toggle ──────────────────────────────────────
    lv_obj_t * row_grid = lv_obj_create(scr_rows);
    lv_obj_set_size(row_grid, 430, 80);
    lv_obj_set_style_bg_color(row_grid, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_grid, 255, 0);
    lv_obj_set_style_border_width(row_grid, 1, 0);
    lv_obj_set_style_border_color(row_grid, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_grid, 12, 0);
    lv_obj_clear_flag(row_grid, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_grid = lv_label_create(row_grid);
    lv_label_set_text(lbl_grid, "Grid Launcher");
    lv_obj_set_style_text_font(lbl_grid, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_grid, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_grid, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * sw_grid = lv_switch_create(row_grid);
    lv_obj_align(sw_grid, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_size(sw_grid, 60, 30);
    lv_obj_set_style_bg_color(sw_grid, lv_color_hex(0xFF6A00), LV_PART_INDICATOR | LV_STATE_CHECKED);
    
    if (use_grid_launcher) lv_obj_add_state(sw_grid, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw_grid, LV_STATE_CHECKED);

    lv_obj_add_event_cb(sw_grid, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        lv_obj_t * obj = lv_event_get_target(e);
        use_grid_launcher = lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1 : 0;
        Preferences p;
        p.begin("hellojimny", false);
        p.putInt("grid_launcher", use_grid_launcher);
        p.end();
        Serial.printf("[SETTINGS] Grid Launcher set to %d\n", use_grid_launcher);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ─── ROW 3: Boot time (Pre-created so we can pass to Row 2's callback) ────
    lv_obj_t * row3 = lv_obj_create(scr_rows);
    lv_obj_set_size(row3, 430, 80);
    lv_obj_set_style_bg_color(row3, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row3, 255, 0);
    lv_obj_set_style_border_width(row3, 1, 0);
    lv_obj_set_style_border_color(row3, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row3, 12, 0);
    lv_obj_clear_flag(row3, LV_OBJ_FLAG_SCROLLABLE);

    // ─── ROW 2: Custom boot img ────────────────────────────────────────────
    lv_obj_t * row2 = lv_obj_create(scr_rows);
    lv_obj_set_size(row2, 430, 80);
    lv_obj_set_style_bg_color(row2, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row2, 255, 0);
    lv_obj_set_style_border_width(row2, 1, 0);
    lv_obj_set_style_border_color(row2, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row2, 12, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    
    // Move row3 down so it appears below row2
    lv_obj_move_foreground(row3);

    lv_obj_t * lbl_boot = lv_label_create(row2);
    lv_label_set_text(lbl_boot, "Custom boot img");
    lv_obj_set_style_text_font(lbl_boot, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_boot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_boot, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * sw_boot = lv_switch_create(row2);
    lv_obj_align(sw_boot, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_size(sw_boot, 60, 30);
    lv_obj_set_style_bg_color(sw_boot, lv_color_hex(0xFF6A00), LV_PART_INDICATOR | LV_STATE_CHECKED);

    // Check persistence to set initial state
    const char* file_on = "/sd_card/boot_img.txt";
    const char* file_off = "/sd_card/boot_img.txt.disabled";
    bool on_exists = (access(file_on, F_OK) == 0);
    bool off_exists = (access(file_off, F_OK) == 0);

    if (on_exists) {
        lv_obj_add_state(sw_boot, LV_STATE_CHECKED);
        lv_obj_clear_flag(row3, LV_OBJ_FLAG_HIDDEN); // Show boot time setting
    } else if (off_exists) {
        lv_obj_clear_state(sw_boot, LV_STATE_CHECKED);
        lv_obj_add_flag(row3, LV_OBJ_FLAG_HIDDEN); // Hide boot time setting
    } else {
        // Neither file exists, disable standard control and label it
        lv_obj_add_state(sw_boot, LV_STATE_DISABLED);
        lv_label_set_text(lbl_boot, "Custom boot img (Not Set)");
        lv_obj_add_flag(row3, LV_OBJ_FLAG_HIDDEN); // Hide boot time setting
    }

    lv_obj_add_event_cb(sw_boot, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        lv_obj_t * obj = lv_event_get_target(e);
        bool is_on = lv_obj_has_state(obj, LV_STATE_CHECKED);
        const char* file_on = "/sd_card/boot_img.txt";
        const char* file_off = "/sd_card/boot_img.txt.disabled";
        lv_obj_t * r3 = (lv_obj_t*)lv_event_get_user_data(e);
        if (is_on) {
            unlink(file_on); // Remove existing destination to ensure rename() succeeds
            rename(file_off, file_on);
            Serial.println("[SETTINGS] Custom boot logo enabled");
            if (r3) lv_obj_clear_flag(r3, LV_OBJ_FLAG_HIDDEN);
        } else {
            unlink(file_off); // Remove existing destination to ensure rename() succeeds
            rename(file_on, file_off);
            Serial.println("[SETTINGS] Custom boot logo disabled");
            if (r3) lv_obj_add_flag(r3, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_VALUE_CHANGED, row3);

    // ─── Build Row 3 (Boot time) elements ────────────────────────────────────
    lv_obj_t * lbl_boot_time = lv_label_create(row3);
    lv_label_set_text(lbl_boot_time, "Boot time");
    lv_obj_set_style_text_font(lbl_boot_time, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_boot_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_boot_time, LV_ALIGN_LEFT_MID, 14, 0);

    // Container for controls
    lv_obj_t * time_cont = lv_obj_create(row3);
    lv_obj_set_size(time_cont, 180, 60);
    lv_obj_align(time_cont, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_opa(time_cont, 0, 0);
    lv_obj_set_style_border_width(time_cont, 0, 0);
    lv_obj_set_style_pad_all(time_cont, 0, 0);
    lv_obj_clear_flag(time_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(time_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Load initial boot time (persistent SD load)
    int current_boot_time = get_custom_boot_time();

    // − button
    lv_obj_t * btn_time_minus = lv_btn_create(time_cont);
    lv_obj_set_size(btn_time_minus, 50, 50);
    lv_obj_set_style_bg_color(btn_time_minus, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_style_radius(btn_time_minus, 25, 0);
    lv_obj_t * lbl_time_minus = lv_label_create(btn_time_minus);
    lv_label_set_text(lbl_time_minus, "-");
    lv_obj_set_style_text_font(lbl_time_minus, &ui_font_rajdhani1, 0);
    lv_obj_center(lbl_time_minus);

    // value label
    lv_obj_t * lbl_time_val = lv_label_create(time_cont);
    lv_label_set_text_fmt(lbl_time_val, "%d s", current_boot_time);
    lv_obj_set_style_text_font(lbl_time_val, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_time_val, lv_color_hex(0xFF9500), 0);

    // + button
    lv_obj_t * btn_time_plus = lv_btn_create(time_cont);
    lv_obj_set_size(btn_time_plus, 50, 50);
    lv_obj_set_style_bg_color(btn_time_plus, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_style_radius(btn_time_plus, 25, 0);
    lv_obj_t * lbl_time_plus = lv_label_create(btn_time_plus);
    lv_label_set_text(lbl_time_plus, "+");
    lv_obj_set_style_text_font(lbl_time_plus, &ui_font_rajdhani1, 0);
    lv_obj_center(lbl_time_plus);

    // Wire callbacks
    lv_obj_add_event_cb(btn_time_minus, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
        int t = get_custom_boot_time();
        if (t > 3) {
            t--;
            set_custom_boot_time(t);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d s", t);
        }
    }, LV_EVENT_ALL, lbl_time_val);

    lv_obj_add_event_cb(btn_time_plus, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
        int t = get_custom_boot_time();
        if (t < 8) {
            t++;
            set_custom_boot_time(t);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d s", t);
        }
    }, LV_EVENT_ALL, lbl_time_val);

    // ─── Swipe-down → back to launcher ───────────────────────────────────────
    lv_obj_add_event_cb(scr, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
            lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
            lv_indev_wait_release(lv_indev_get_act());
            switch_to_launcher();
        }
    }, LV_EVENT_ALL, NULL);
}


// ─── OTA UPDATE OVERLAY ──────────────────────────────────────────────────────
// Shared with the simulator (sim/) — see ota_overlay_ui.h. Editing that file
// changes what BOTH the device and the sim render; there's only one copy.
#include "ota_overlay_ui.h"

// ─── ABOUT SCREEN ────────────────────────────────────────────────────────────
static lv_obj_t * about_screen = NULL;

void build_about_screen() {
    if (about_screen == NULL) {
        about_screen = lv_obj_create(NULL);
    } else {
        lv_obj_clean(about_screen);
    }
    lv_scr_load_anim(about_screen, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, false);
    currentMode = MODE_UI;

    lv_obj_t * scr = about_screen;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title bar ──────────────────────────────────────────────────────────
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "ABOUT");
    lv_obj_set_style_text_font(title, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    // ── Page container (horizontal scroll, snap, full-width pages) ─────────
    // SCREEN_WIDTH = 466
    lv_obj_t * pages = lv_obj_create(scr);
    lv_obj_set_size(pages, 466, 360);
    lv_obj_align(pages, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_snap_x(pages, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(pages, 0, 0);
    lv_obj_set_style_border_width(pages, 0, 0);
    lv_obj_set_style_pad_all(pages, 0, 0);
    lv_obj_set_style_pad_column(pages, 0, 0);
    lv_obj_clear_flag(pages, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pages, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(pages, LV_DIR_HOR);

    // ── Pagination dots ───────────────────────────────────────────────────
    lv_obj_t * dot_cont = lv_obj_create(scr);
    lv_obj_set_size(dot_cont, 60, 16);
    lv_obj_align(dot_cont, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_opa(dot_cont, 0, 0);
    lv_obj_set_style_border_width(dot_cont, 0, 0);
    lv_obj_set_flex_flow(dot_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_cont, 8, 0);
    lv_obj_clear_flag(dot_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    static const uint32_t DOT_ACT   = 0xFF9800; // orange active
    static const uint32_t DOT_INACT = 0x444444; // grey inactive
    const int NUM_PAGES = 2;

    lv_obj_t * dots[NUM_PAGES];
    for (int i = 0; i < NUM_PAGES; i++) {
        dots[i] = lv_obj_create(dot_cont);
        lv_obj_set_size(dots[i], 10, 10);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == 0 ? DOT_ACT : DOT_INACT), 0);
        lv_obj_set_style_border_width(dots[i], 0, 0);
        lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    // ══════════════════════════════════════════════════════════════════════
    // PAGE 1 — Gesture hints
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t * page1 = lv_obj_create(pages);
    lv_obj_set_size(page1, 466, 360);
    lv_obj_set_style_bg_opa(page1, 0, 0);
    lv_obj_set_style_border_width(page1, 0, 0);
    lv_obj_set_style_pad_all(page1, 0, 0);
    lv_obj_clear_flag(page1, LV_OBJ_FLAG_SCROLLABLE);

    // ─ Hint 1: Swipe down to go back ─────────────────────────────────────
    // Single down-arrow icon drawn as a label symbol
    lv_obj_t * swipe_icon = lv_label_create(page1);
    lv_label_set_text(swipe_icon, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(swipe_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(swipe_icon, lv_color_hex(0xFF9800), 0);
    lv_obj_align(swipe_icon, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t * swipe_lbl = lv_label_create(page1);
    lv_label_set_text(swipe_lbl, "Swipe down to exit\nto previous screen");
    lv_obj_set_style_text_font(swipe_lbl, &lv_font_montserrat_20, 0); // +20% from 16
    lv_obj_set_style_text_color(swipe_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(swipe_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(swipe_lbl, swipe_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    // ─ Divider line ───────────────────────────────────────────────────────
    lv_obj_t * divider = lv_obj_create(page1);
    lv_obj_set_size(divider, 280, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 158);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // ─ Hint 2: Left/Right + Long press ────────────────────────────────────
    // Row: LV_SYMBOL_LEFT  [concentric circles]  LV_SYMBOL_RIGHT
    lv_obj_t * hint2_row = lv_obj_create(page1);
    lv_obj_set_size(hint2_row, 400, 80);
    lv_obj_set_style_bg_opa(hint2_row, 0, 0);
    lv_obj_set_style_border_width(hint2_row, 0, 0);
    lv_obj_set_style_pad_all(hint2_row, 0, 0);
    lv_obj_align(hint2_row, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_clear_flag(hint2_row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Left arrow
    lv_obj_t * arr_left = lv_label_create(hint2_row);
    lv_label_set_text(arr_left, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(arr_left, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(arr_left, lv_color_hex(0xFF9800), 0);
    lv_obj_align(arr_left, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_clear_flag(arr_left, LV_OBJ_FLAG_CLICKABLE);

    // Concentric circles in the centre of the row
    lv_obj_t * circle_wrap = lv_obj_create(hint2_row);
    lv_obj_set_size(circle_wrap, 70, 70);
    lv_obj_set_style_bg_opa(circle_wrap, 0, 0);
    lv_obj_set_style_border_width(circle_wrap, 0, 0);
    lv_obj_align(circle_wrap, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(circle_wrap, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * c_outer = lv_obj_create(circle_wrap);
    lv_obj_set_size(c_outer, 66, 66);
    lv_obj_set_style_radius(c_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c_outer, 0, 0);
    lv_obj_set_style_border_color(c_outer, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_border_width(c_outer, 2, 0);
    lv_obj_center(c_outer);
    lv_obj_clear_flag(c_outer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * c_mid = lv_obj_create(circle_wrap);
    lv_obj_set_size(c_mid, 44, 44);
    lv_obj_set_style_radius(c_mid, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c_mid, 0, 0);
    lv_obj_set_style_border_color(c_mid, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_border_width(c_mid, 2, 0);
    lv_obj_center(c_mid);
    lv_obj_clear_flag(c_mid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * c_inner = lv_obj_create(circle_wrap);
    lv_obj_set_size(c_inner, 20, 20);
    lv_obj_set_style_radius(c_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c_inner, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_border_width(c_inner, 0, 0);
    lv_obj_center(c_inner);
    lv_obj_clear_flag(c_inner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Right arrow
    lv_obj_t * arr_right = lv_label_create(hint2_row);
    lv_label_set_text(arr_right, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arr_right, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(arr_right, lv_color_hex(0xFF9800), 0);
    lv_obj_align(arr_right, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_clear_flag(arr_right, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * longpress_lbl = lv_label_create(page1);
    lv_label_set_text(longpress_lbl, "Left/Right & long press lets\nyou explore more options");
    lv_obj_set_style_text_font(longpress_lbl, &lv_font_montserrat_20, 0); // +20% from 16
    lv_obj_set_style_text_color(longpress_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(longpress_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(longpress_lbl, hint2_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    // Swipe hint text at bottom of page 1
    lv_obj_t * swipe_hint1 = lv_label_create(page1);
    lv_label_set_text(swipe_hint1, "Swipe " LV_SYMBOL_RIGHT " for more");
    lv_obj_set_style_text_font(swipe_hint1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(swipe_hint1, lv_color_hex(0x555555), 0);
    lv_obj_align(swipe_hint1, LV_ALIGN_BOTTOM_MID, 0, -28);


    // ══════════════════════════════════════════════════════════════════════
    // PAGE 2 — "Life is Jimny" + J74CREW + Software Update
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t * page2 = lv_obj_create(pages);
    lv_obj_set_size(page2, 466, 360);
    lv_obj_set_style_bg_opa(page2, 0, 0);
    lv_obj_set_style_border_width(page2, 0, 0);
    lv_obj_set_style_pad_all(page2, 0, 0);
    lv_obj_clear_flag(page2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page2, LV_OBJ_FLAG_SCROLLABLE); // Make scrollable vertically
    lv_obj_set_scroll_dir(page2, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(page2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(page2, 10, 0);
    lv_obj_set_style_pad_bottom(page2, 40, 0);
    lv_obj_set_style_pad_gap(page2, 10, 0);

    extern const lv_img_dsc_t ui_img_trailmaster;
    lv_obj_t * about_logo = lv_img_create(page2);
    lv_img_set_src(about_logo, &ui_img_trailmaster);
    lv_img_set_zoom(about_logo, 160); // smaller logo (was 276)
    // Reduce padding below logo to 5px gap before version text
    lv_obj_set_style_pad_bottom(about_logo, 0, 0);

    lv_obj_t * ver_lbl = lv_label_create(page2);
    lv_label_set_text_fmt(ver_lbl, "v%s", ota_current_version());
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_24, 0); // larger, legible version
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_top(ver_lbl, 0, 0);

    lv_obj_t * scr_rows = page2;

    // 1. Check for Update / Install button
    lv_obj_t * row_update_btn = lv_obj_create(scr_rows);
    lv_obj_set_size(row_update_btn, 430, 80);
    lv_obj_set_style_bg_opa(row_update_btn, 0, 0);
    lv_obj_set_style_border_width(row_update_btn, 0, 0);
    lv_obj_clear_flag(row_update_btn, LV_OBJ_FLAG_SCROLLABLE);

    static lv_obj_t * btn_check = NULL;
    btn_check = lv_btn_create(row_update_btn);
    lv_obj_set_size(btn_check, 400, 64);
    lv_obj_align(btn_check, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btn_check, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_style_radius(btn_check, 16, 0);
    static lv_obj_t * lbl_btn_check = NULL;
    lbl_btn_check = lv_label_create(btn_check);
    lv_label_set_text(lbl_btn_check, "Check for Update");
    lv_obj_set_style_text_font(lbl_btn_check, &ui_font_rajdhani1, 0);
    lv_obj_center(lbl_btn_check);

    // 2. "Wifi settings" — plain tappable text (no button box)
    lv_obj_t * lbl_wifi = lv_label_create(scr_rows);
    lv_label_set_text(lbl_wifi, "Wifi settings");
    lv_obj_set_style_text_font(lbl_wifi, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_ver(lbl_wifi, 14, 0);          // taller hit area
    lv_obj_add_flag(lbl_wifi, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(lbl_wifi, 24);            // easier to tap

    extern void pf_show_upload_overlay(void);
    lv_obj_add_event_cb(lbl_wifi, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) pf_show_upload_overlay();
    }, LV_EVENT_ALL, NULL);

    // "Check for Update" opens the OTA overlay; all status/progress shows there.
    lv_obj_add_event_cb(btn_check, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_ota_update_overlay();
    }, LV_EVENT_ALL, NULL);



    // ── Dot update on scroll ───────────────────────────────────────────────
    // Store page container pointer and dots in a small struct on the heap
    struct AboutPageData {
        lv_obj_t * pages;
        lv_obj_t * dot0;
        lv_obj_t * dot1;
    };
    AboutPageData * pd = new AboutPageData { pages, dots[0], dots[1] };

    lv_obj_add_event_cb(pages, [](lv_event_t * ev) {
        AboutPageData * d = (AboutPageData *)lv_event_get_user_data(ev);
        lv_coord_t scroll_x = lv_obj_get_scroll_x(d->pages);
        int page_idx = (scroll_x + 233) / 466; // snap to nearest page
        lv_obj_set_style_bg_color(d->dot0, lv_color_hex(page_idx == 0 ? 0xFF9800 : 0x444444), 0);
        lv_obj_set_style_bg_color(d->dot1, lv_color_hex(page_idx == 1 ? 0xFF9800 : 0x444444), 0);
    }, LV_EVENT_SCROLL, pd);

    // ── Swipe down → back to launcher ─────────────────────────────────────
    lv_obj_add_event_cb(scr, [](lv_event_t * ev) {
        if (lv_event_get_code(ev) == LV_EVENT_GESTURE &&
            lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
            lv_indev_wait_release(lv_indev_get_act());
            switch_to_launcher();
        }
    }, LV_EVENT_ALL, NULL);
}


void build_rom_menu() {
    currentMode = MODE_NES_BROWSER;
    game_selected = false;
    selected_rom_path[0] = '\0';
    if (rom_screen == NULL) {
        rom_screen = lv_obj_create(NULL);
    } else {
        lv_obj_clean(rom_screen); // Clear old menu without deleting the active screen!
    }
    lv_scr_load_anim(rom_screen, LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false);
    
    lv_obj_t * scr = rom_screen;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "SELECT GAME");
    lv_obj_set_style_text_font(title, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    rom_list = lv_obj_create(scr);
    lv_obj_set_size(rom_list, 466, 380);
    lv_obj_align(rom_list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_flex_flow(rom_list, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rom_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_snap_x(rom_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(rom_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(rom_list, 0, 0);
    lv_obj_set_style_border_width(rom_list, 0, 0);
    lv_obj_set_style_pad_column(rom_list, 20, 0); 
    lv_obj_set_style_pad_left(rom_list, 123, 0);  
    lv_obj_set_style_pad_right(rom_list, 123, 0);

    // Helper to create NES ROM cards
    // Helper to create NES ROM cards
    auto create_nes_card = [](const char* clean_name) {
        lv_obj_t * btn = lv_btn_create(rom_list);
        lv_obj_set_size(btn, 220, 340);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_clip_corner(btn, true, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        char img_path_bmp[128];
        snprintf(img_path_bmp, sizeof(img_path_bmp), "/sd_card/nes/%s.bmp", clean_name);
        
        lv_img_dsc_t * dsc = load_bmp_to_psram(img_path_bmp);
        
        if (dsc) {
            lv_obj_t * img = lv_img_create(btn);
            lv_img_set_src(img, dsc);
            lv_obj_set_width(img, 220);
            lv_obj_set_height(img, 280);
            lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
            
            lv_obj_add_event_cb(img, [](lv_event_t * e) {
                lv_img_dsc_t * d = (lv_img_dsc_t *)lv_event_get_user_data(e);
                if (d) {
                    if (d->data) heap_caps_free((void*)d->data);
                    heap_caps_free(d);
                }
            }, LV_EVENT_DELETE, dsc);
        } else {
            lv_obj_t * placeholder = lv_obj_create(btn);
            lv_obj_set_size(placeholder, 220, 280);
            lv_obj_set_style_bg_color(placeholder, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(placeholder, 0, 0);
            lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t * lblIcon = lv_label_create(placeholder);
            lv_label_set_text(lblIcon, LV_SYMBOL_FILE);
            lv_obj_set_style_text_font(lblIcon, &lv_font_montserrat_48, 0);
            lv_obj_center(lblIcon);
            lv_obj_clear_flag(lblIcon, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, clean_name);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lbl, 200);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_bottom(lbl, 15, 0);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

        char * filename = strdup(clean_name);
        lv_obj_add_event_cb(btn, [](lv_event_t * e) {
            if (ignore_until_lift) return; 
            const char * fname = (const char *)lv_event_get_user_data(e);
            snprintf(selected_rom_path, sizeof(selected_rom_path), "/sd_card/nes/%s.nes", fname);
            game_selected = true;
        }, LV_EVENT_SHORT_CLICKED, filename);
        
        lv_obj_add_event_cb(btn, [](lv_event_t * e) {
            void * ptr = lv_event_get_user_data(e);
            if (ptr) free(ptr);
        }, LV_EVENT_DELETE, filename);
    };

    // Helper to create Dino card
    auto create_dino_card = []() {
        lv_obj_t * btnDino = lv_btn_create(rom_list);
        lv_obj_set_size(btnDino, 220, 340);
        lv_obj_set_style_bg_color(btnDino, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(btnDino, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(btnDino, 2, 0);
        lv_obj_set_style_radius(btnDino, 16, 0);
        lv_obj_set_style_pad_all(btnDino, 0, 0);
        lv_obj_set_style_clip_corner(btnDino, true, 0);
        lv_obj_set_flex_flow(btnDino, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btnDino, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        const char * dino_paths[] = {
            "/sd_card/nes/dino.bmp",
            "/sd_card/nes/Dino.bmp",
            "/sd_card/nes/dinorun.bmp",
            "/sd_card/nes/dino_run.bmp",
            "/sd_card/nes/DinoRun.bmp",
            "/sd_card/nes/Dino_Run.bmp",
            "/sd_card/nes/Dino Run.bmp",
            "/sd_card/dino.bmp",
            "/sd_card/Dino.bmp",
            "/sd_card/dinorun.bmp",
            "/sd_card/dino_run.bmp",
            "/sd_card/DinoRun.bmp",
            "/sd_card/Dino_Run.bmp",
            "/sd_card/Dino Run.bmp"
        };
        lv_img_dsc_t * dsc_dino = nullptr;
        for (const char * path : dino_paths) {
            dsc_dino = load_bmp_to_psram(path);
            if (dsc_dino) break;
        }
        
        if (dsc_dino) {
            lv_obj_t * imgDino = lv_img_create(btnDino);
            lv_img_set_src(imgDino, dsc_dino);
            lv_obj_set_width(imgDino, 220);
            lv_obj_set_height(imgDino, 280);
            lv_obj_clear_flag(imgDino, LV_OBJ_FLAG_CLICKABLE);
            
            lv_obj_add_event_cb(imgDino, [](lv_event_t * e) {
                lv_img_dsc_t * d = (lv_img_dsc_t *)lv_event_get_user_data(e);
                if (d) {
                    if (d->data) heap_caps_free((void*)d->data);
                    heap_caps_free(d);
                }
            }, LV_EVENT_DELETE, dsc_dino);
        } else {
            lv_obj_t * placeholder = lv_obj_create(btnDino);
            lv_obj_set_size(placeholder, 220, 280);
            lv_obj_set_style_bg_color(placeholder, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(placeholder, 0, 0);
            lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t * iconDino = lv_label_create(placeholder);
            lv_label_set_text(iconDino, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_font(iconDino, &lv_font_montserrat_48, 0);
            lv_obj_center(iconDino);
            lv_obj_clear_flag(iconDino, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t * lblDino = lv_label_create(btnDino);
        lv_label_set_text(lblDino, "DINO RUN");
        lv_label_set_long_mode(lblDino, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lblDino, 200);
        lv_obj_set_style_text_align(lblDino, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lblDino, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_bottom(lblDino, 15, 0);
        lv_obj_clear_flag(lblDino, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(btnDino, [](lv_event_t * e) {
            if (ignore_until_lift) return; 
            activeGame = GAME_DINO;
            currentMode = MODE_GAME; is_game_over = false;
            reset_obstacles();
            game_timer = millis(); amoled.fillScreen(0xFFFF); 
            last_restart_time = millis(); 
            lv_scr_load_anim(ui_uiScreenGame, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
        }, LV_EVENT_SHORT_CLICKED, NULL);
    };

    // Helper to create Flappy card
    auto create_flappy_card = []() {
        lv_obj_t * btnFlappy = lv_btn_create(rom_list);
        lv_obj_set_size(btnFlappy, 220, 340);
        lv_obj_set_style_bg_color(btnFlappy, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(btnFlappy, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(btnFlappy, 2, 0);
        lv_obj_set_style_radius(btnFlappy, 16, 0);
        lv_obj_set_style_pad_all(btnFlappy, 0, 0);
        lv_obj_set_style_clip_corner(btnFlappy, true, 0);
        lv_obj_set_flex_flow(btnFlappy, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btnFlappy, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        const char * flappy_paths[] = {
            "/sd_card/nes/flappy.bmp",
            "/sd_card/nes/Flappy.bmp",
            "/sd_card/nes/flappybird.bmp",
            "/sd_card/nes/flappy_bird.bmp",
            "/sd_card/nes/FlappyBird.bmp",
            "/sd_card/nes/Flappy_Bird.bmp",
            "/sd_card/nes/Flappy Bird.bmp",
            "/sd_card/flappy.bmp",
            "/sd_card/Flappy.bmp",
            "/sd_card/flappybird.bmp",
            "/sd_card/flappy_bird.bmp",
            "/sd_card/FlappyBird.bmp",
            "/sd_card/Flappy_Bird.bmp",
            "/sd_card/Flappy Bird.bmp"
        };
        lv_img_dsc_t * dsc_flappy = nullptr;
        for (const char * path : flappy_paths) {
            dsc_flappy = load_bmp_to_psram(path);
            if (dsc_flappy) break;
        }
        
        if (dsc_flappy) {
            lv_obj_t * imgFlappy = lv_img_create(btnFlappy);
            lv_img_set_src(imgFlappy, dsc_flappy);
            lv_obj_set_width(imgFlappy, 220);
            lv_obj_set_height(imgFlappy, 280);
            lv_obj_clear_flag(imgFlappy, LV_OBJ_FLAG_CLICKABLE);
            
            lv_obj_add_event_cb(imgFlappy, [](lv_event_t * e) {
                lv_img_dsc_t * d = (lv_img_dsc_t *)lv_event_get_user_data(e);
                if (d) {
                    if (d->data) heap_caps_free((void*)d->data);
                    heap_caps_free(d);
                }
            }, LV_EVENT_DELETE, dsc_flappy);
        } else {
            lv_obj_t * placeholder = lv_obj_create(btnFlappy);
            lv_obj_set_size(placeholder, 220, 280);
            lv_obj_set_style_bg_color(placeholder, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(placeholder, 0, 0);
            lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t * iconFlappy = lv_label_create(placeholder);
            lv_label_set_text(iconFlappy, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_font(iconFlappy, &lv_font_montserrat_48, 0);
            lv_obj_center(iconFlappy);
            lv_obj_clear_flag(iconFlappy, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t * lblFlappy = lv_label_create(btnFlappy);
        lv_label_set_text(lblFlappy, "FLAPPY BIRD");
        lv_label_set_long_mode(lblFlappy, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lblFlappy, 200);
        lv_obj_set_style_text_align(lblFlappy, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lblFlappy, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_bottom(lblFlappy, 15, 0);
        lv_obj_clear_flag(lblFlappy, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(btnFlappy, [](lv_event_t * e) {
            if (ignore_until_lift) return; 
            activeGame = GAME_FLAPPY;
            currentMode = MODE_GAME;
            reset_flappy_game();
            game_timer = millis(); amoled.fillScreen(0xFFFF); 
            last_restart_time = millis(); 
            lv_scr_load_anim(ui_uiScreenGame, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
        }, LV_EVENT_SHORT_CLICKED, NULL);
    };

    // Scan SD card into array
    char roms[30][64];
    int rom_count = 0;
    DIR *dir = opendir("/sd_card/nes/");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && rom_count < 30) {
            if (ent->d_name[0] == '.') continue;
            if (strstr(ent->d_name, ".nes") || strstr(ent->d_name, ".NES")) {
                char clean_name[64];
                strncpy(clean_name, ent->d_name, sizeof(clean_name)-1);
                char *dot = strrchr(clean_name, '.'); if (dot) *dot = '\0';
                strncpy(roms[rom_count], clean_name, 63);
                roms[rom_count][63] = '\0';
                rom_count++;
            }
        }
        closedir(dir);
    }

    auto get_prio = [](const char* name) -> int {
        if (strcasestr(name, "flappy") || strcasestr(name, "Flappy")) return 1;
        if (strcasestr(name, "galaga") || strcasestr(name, "Galaga")) return 3;
        if (strcasestr(name, "road") || strcasestr(name, "Road")) return 4;
        return 10;
    };

    // Build the UI in the exact requested order
    create_flappy_card();
    create_dino_card();
    for(int i=0; i<rom_count; i++) if (get_prio(roms[i]) == 3) create_nes_card(roms[i]);
    for(int i=0; i<rom_count; i++) if (get_prio(roms[i]) == 4) create_nes_card(roms[i]);
    for(int i=0; i<rom_count; i++) if (get_prio(roms[i]) == 10) create_nes_card(roms[i]);

    force_full_ui_redraw(rom_screen);
}

// Clear panel pixels written outside LVGL (e.g. RetroEngine::flush), then redraw the whole target screen.
void force_full_ui_redraw(lv_obj_t * target_scr) {
    // amoled.fillScreen(AMOLED_COLOR_BLACK); // Removed to fix screen flash when returning to launcher
    lv_disp_t * disp = lv_disp_get_default();
    if (disp && disp->driver) {
        disp->driver->full_refresh = 1;
    }
    lv_obj_t * scr = target_scr ? target_scr : lv_scr_act();
    if (scr) {
        lv_obj_invalidate(scr);
    }
    lv_refr_now(disp);
    if (disp && disp->driver) {
        disp->driver->full_refresh = 0;
    }
}

// --- CORE SYSTEM DRIVERS ---

__attribute__((hot)) __attribute__((optimize("O3")))
void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    
    // High-performance 32-bit parallel byte-swapping (swaps two pixels at once)
    uint32_t len32 = len / 2;
    uint32_t *p32 = (uint32_t *)color_p;
    for (uint32_t i = 0; i < len32; i++) {
        uint32_t v = p32[i];
        p32[i] = ((v & 0xFF00FF00UL) >> 8) | ((v & 0x00FF00FFUL) << 8);
    }
    if (len & 1) {
        uint16_t color = color_p[len - 1].full;
        color_p[len - 1].full = (color << 8) | (color >> 8);
    }
    
    amoled.drawArea(area->x1, area->y1, area->x2, area->y2, (uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

void exit_launcher() {
    lv_obj_t * target = last_screen_before_launcher;
    if (!target) target = ui_uispeedometer; 
    if (target) {
        lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
        if (target == ui_uiScreenGame) currentMode = MODE_GAME;
        else if (target == ui_uiinclinometer) currentMode = MODE_INCLINOMETER;
        else if (target == photoframe_screen) currentMode = MODE_PHOTOFRAME;
        else currentMode = MODE_UI;
    }
}

static void launcher_overscroll_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_TOP) { // Swipe UP (finger moves up)
            // Removed exit_launcher() here to prevent accidental launches
        }
    }
}

static bool launcher_pending = false;

#include <sys/stat.h>
void take_screenshot() {
    lv_img_dsc_t * snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (!snap) {
        Serial.println("[SCREENSHOT] Failed to allocate snapshot!");
        return;
    }
    
    struct stat st;
    if (stat("/sd_card/screenshots", &st) != 0) {
        mkdir("/sd_card/screenshots", 0777);
    }
    
    char filename[64];
    for(int i=0; i<10000; i++) {
        sprintf(filename, "/sd_card/screenshots/scr_%04d.bmp", i);
        if (stat(filename, &st) != 0) break;
    }
    
    FILE * f = fopen(filename, "wb");
    if (f) {
        uint32_t width = snap->header.w;
        uint32_t height = snap->header.h;
        int32_t neg_height = -height;
        uint32_t row_size = (width * 3 + 3) & ~3;
        uint32_t pixel_data_size = row_size * height;
        uint32_t file_size = 54 + pixel_data_size;
        
        uint8_t header[54] = {
            'B', 'M',
            (uint8_t)(file_size & 0xFF), (uint8_t)((file_size >> 8) & 0xFF), (uint8_t)((file_size >> 16) & 0xFF), (uint8_t)((file_size >> 24) & 0xFF),
            0, 0, 0, 0,
            54, 0, 0, 0,
            40, 0, 0, 0,
            (uint8_t)(width & 0xFF), (uint8_t)((width >> 8) & 0xFF), (uint8_t)((width >> 16) & 0xFF), (uint8_t)((width >> 24) & 0xFF),
            (uint8_t)(neg_height & 0xFF), (uint8_t)((neg_height >> 8) & 0xFF), (uint8_t)((neg_height >> 16) & 0xFF), (uint8_t)((neg_height >> 24) & 0xFF),
            1, 0,
            24, 0,
            0, 0, 0, 0,
            (uint8_t)(pixel_data_size & 0xFF), (uint8_t)((pixel_data_size >> 8) & 0xFF), (uint8_t)((pixel_data_size >> 16) & 0xFF), (uint8_t)((pixel_data_size >> 24) & 0xFF),
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0
        };
        fwrite(header, 1, 54, f);
        
        uint8_t * buf = (uint8_t*)snap->data;
        uint8_t * row_buf = (uint8_t*)heap_caps_malloc(row_size, MALLOC_CAP_SPIRAM);
        if (row_buf) {
            for (uint32_t y = 0; y < height; y++) {
                int out_idx = 0;
                for (uint32_t x = 0; x < width; x++) {
                    uint16_t color16 = ((uint16_t*)buf)[y * width + x];
                    // RGB565 to RGB888 (BMP is BGR)
                    uint8_t r = (color16 >> 11) & 0x1F;
                    uint8_t g = (color16 >> 5) & 0x3F;
                    uint8_t b = color16 & 0x1F;
                    row_buf[out_idx++] = (b * 255) / 31;
                    row_buf[out_idx++] = (g * 255) / 63;
                    row_buf[out_idx++] = (r * 255) / 31;
                }
                while(out_idx % 4 != 0) {
                    row_buf[out_idx++] = 0;
                }
                fwrite(row_buf, 1, out_idx, f);
            }
            heap_caps_free(row_buf);
        }
        fclose(f);
        Serial.printf("[SCREENSHOT] Saved %s\n", filename);
    } else {
        Serial.printf("[SCREENSHOT] Failed to open %s for writing\n", filename);
    }
    lv_snapshot_free(snap);
}

void force_touch_reset() {
    ignore_until_lift = true;
}

void my_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    static bool p_state = false;
    static uint32_t last_touch_time = 0;
    
    if (getTouch(&x, &y)) {
        last_touch_time = millis();
        data->point.x = x; 
        data->point.y = y;
        
        if (ignore_until_lift) {
            data->state = LV_INDEV_STATE_REL;
            return;
        }
        
        if (!p_state) { touch_start_y = y; p_state = true; }
        
        // SWIPE DOWN TO LAUNCHER (from any screen except launcher itself)
        if (lv_scr_act() != ui_uilauncher && (y - touch_start_y > 100)) {
            if (currentMode == MODE_GAME) {
                // Return to ROM Menu from Dino Game
                stop_all_games();
                build_rom_menu();
                p_state = false; 
                ignore_until_lift = true;
                data->point.x = x; 
                data->point.y = y;
                data->state = LV_INDEV_STATE_REL; 
                return;
            } else {
                // Default: Return to Jimny Launcher
                launcher_pending = true;
                p_state = false; 
                ignore_until_lift = true;
                data->point.x = x; 
                data->point.y = y;
                data->state = LV_INDEV_STATE_REL; 
                return; 
            }
        }
        
        // SWIPE UP TO TAKE SCREENSHOT
        if (touch_start_y - y > 100) {
            take_screenshot();
            p_state = false; 
            ignore_until_lift = true;
            data->point.x = x; 
            data->point.y = y;
            data->state = LV_INDEV_STATE_REL; 
            return;
        }
        
        // Swipe up to exit launcher functionality removed per user request
        data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PR; 
    } else { 
        p_state = false; 
        if (millis() - last_touch_time > 100) {
            ignore_until_lift = false;
        }
        data->state = LV_INDEV_STATE_REL; 
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); // HARDWARE COOL-DOWN: Prevents SD 0x101 errors after reboot
    
    if (!amoled.begin()) while (1);
    SD_card_Init();
    FFat.begin(); 
    Touch_Init(); 
    
    RetroEngine::begin();
    lv_init();
    
    lvgl_buf1 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lvgl_buf2 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!lvgl_buf1) {
        lvgl_buf1 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, lvgl_buf2, LVGL_DRAW_BUF_SIZE);
    
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH; disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush; disp_drv.draw_buf = &draw_buf;
    disp_drv.antialiasing = 1;
    lv_disp_drv_register(&disp_drv);
    
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER; indev_drv.read_cb = my_touch_read;
    lv_indev_drv_register(&indev_drv);
    
    // Load custom Godzilla still image: try rpm_idle.bmp first, fallback to gojira.bmp
    dsc_godzilla = load_bmp_to_psram("/sd_card/speedometergif/rpm_idle.bmp");
    if (!dsc_godzilla) {
        dsc_godzilla = load_bmp_to_psram("/sd_card/gojira.bmp");
    }

    // Load jimny_logo preference BEFORE show_boot_splash so the correct splash is shown
    {
        Preferences p;
        p.begin("hellojimny", false);
        use_jimny_logo = p.getInt("jimny_logo", 0); // Default to 0 (Trailmaster)
        use_grid_launcher = p.getInt("grid_launcher", 1); // Default to 1 (Grid launcher on)
        p.end();
        Serial.printf("[SETTINGS] Pre-splash: grid_launcher = %d, jimny_logo = %d\n", use_grid_launcher, use_jimny_logo);
    }
    
    lv_obj_t * splash = show_boot_splash(); 
    ui_init(); 

    // Settings already loaded above — no need to re-read
    Serial.printf("[SETTINGS] Post-init: grid_launcher = %d, jimny_logo = %d\n", use_grid_launcher, use_jimny_logo);


    extern lv_obj_t * ui_Image10;
    extern lv_obj_t * ui_Image7;
    extern lv_obj_t * ui_Image8;
    extern lv_obj_t * ui_Image3;
    extern const lv_img_dsc_t ui_img_trailmaster;
    extern const lv_img_dsc_t ui_img_1814113988;
    
    // Launcher is always Trailmaster
    if (ui_Image10) { lv_img_set_src(ui_Image10, &ui_img_trailmaster); lv_img_set_zoom(ui_Image10, 230); lv_obj_set_x(ui_Image10, 0); }
    
    const lv_img_dsc_t* other_logo = use_jimny_logo ? &ui_img_1814113988 : &ui_img_trailmaster;
    if (ui_Image7) { lv_img_set_src(ui_Image7, other_logo); lv_img_set_zoom(ui_Image7, use_jimny_logo ? 200 : 230); lv_obj_set_x(ui_Image7, 0); }
    if (ui_Image8) { lv_img_set_src(ui_Image8, other_logo); lv_img_set_zoom(ui_Image8, 150); lv_obj_set_y(ui_Image8, -112); }
    if (ui_Image3) { lv_img_set_src(ui_Image3, other_logo); lv_img_set_zoom(ui_Image3, use_jimny_logo ? 200 : 230); lv_obj_set_x(ui_Image3, 0); } // Same as inclinometer

    if (ui_uilauncher) {
        lv_obj_add_event_cb(ui_uilauncher, [](lv_event_t * e) {
            if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
                if (use_grid_launcher) {
                    if (ui_Panel1) lv_obj_add_flag(ui_Panel1, LV_OBJ_FLAG_HIDDEN);
                    if (sel_bar) lv_obj_add_flag(sel_bar, LV_OBJ_FLAG_HIDDEN);
                    build_grid_launcher();
                } else {
                    if (ui_Panel1) lv_obj_clear_flag(ui_Panel1, LV_OBJ_FLAG_HIDDEN);
                    if (sel_bar) lv_obj_clear_flag(sel_bar, LV_OBJ_FLAG_HIDDEN);
                    if (grid_container != NULL) {
                        lv_obj_del(grid_container);
                        grid_container = NULL;
                    }
                    if (ui_Panel1) lv_obj_scroll_to_y(ui_Panel1, 0, LV_ANIM_OFF);
                }
            }
        }, LV_EVENT_SCREEN_LOADED, NULL);
    }


    
    if (ui_versionlabel) {
        lv_obj_add_flag(ui_versionlabel, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Injected Graphic Expansion: Speedometer Dynamic RPM Gauge
    if (ui_uispeedometer) {
        build_speedo_rpm_gauge(ui_uispeedometer);
    }
    
    photoframe_setup(); build_photoframe_screen();

    ota_init();  // initialize OTA status (safe; no network activity)

    if(ui_uilauncher && ui_Panel1) {
        lv_obj_add_event_cb(ui_Panel1, launcher_overscroll_cb, LV_EVENT_ALL, NULL);

        // Override Panel1 y-offset: SquareLine set y=71 which shifts Panel1 center to y=304.
        // We reset it to 0 so Panel1 center == screen center (466/2 = 233).
        lv_obj_set_y(ui_Panel1, 0);

        // Enable center-snap so every swipe lands a button in the orange bar
        lv_obj_set_scroll_snap_y(ui_Panel1, LV_SCROLL_SNAP_CENTER);
        lv_obj_clear_flag(ui_Panel1, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        // Symmetric padding: (panel_height - button_height) / 2 = (331 - 83) / 2 = 124
        lv_obj_set_style_pad_top(ui_Panel1, 124, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(ui_Panel1, 124, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Make all buttons inside the panel non-clickable directly
        // The orange bar is the ONLY clickable element
        uint32_t child_cnt = lv_obj_get_child_cnt(ui_Panel1);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_clear_flag(lv_obj_get_child(ui_Panel1, i), LV_OBJ_FLAG_CLICKABLE);
        }

        // Orange selection bar — centered on screen (screen center = 466/2 = 233)
        // Bar height=83, so bar top = 233-41 = 192, bar bottom = 274
        sel_bar = lv_obj_create(ui_uilauncher);
        lv_obj_set_size(sel_bar, 420, 83);
        lv_obj_align(sel_bar, LV_ALIGN_CENTER, 0, 0);  // true screen center
        lv_obj_set_style_bg_color(sel_bar, lv_color_hex(0xFF6A00), 0);
        lv_obj_set_style_bg_opa(sel_bar, 90, 0);
        lv_obj_set_style_border_color(sel_bar, lv_color_hex(0xFF9500), 0);
        lv_obj_set_style_border_width(sel_bar, 2, 0);
        lv_obj_set_style_border_width(sel_bar, 3, 0);         // thicker = uniform at corners
        lv_obj_set_style_border_opa(sel_bar, 255, 0);          // fully opaque - no fading
        lv_obj_set_style_radius(sel_bar, 14, 0);
        lv_obj_set_style_outline_width(sel_bar, 0, 0);         // suppress any ghost outline
        lv_obj_clear_flag(sel_bar, LV_OBJ_FLAG_SCROLLABLE);

        // Shrink the CLICKABLE hit area to match button width (405px) not the frame (420px)
        // Use a child click-zone object instead of the visual bar itself
        lv_obj_clear_flag(sel_bar, LV_OBJ_FLAG_CLICKABLE);    // bar is visual-only
        lv_obj_t * click_zone = lv_obj_create(sel_bar);
        lv_obj_set_size(click_zone, 405, 73);                  // button width, slightly shorter
        lv_obj_align(click_zone, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_opa(click_zone, 0, LV_STATE_DEFAULT);             // invisible normally
        lv_obj_set_style_border_width(click_zone, 0, LV_STATE_DEFAULT);
        
        // Add visual feedback when pressed
        lv_obj_set_style_bg_color(click_zone, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(click_zone, 100, LV_STATE_PRESSED);
        lv_obj_set_style_radius(click_zone, 12, LV_STATE_DEFAULT);

        lv_obj_clear_flag(click_zone, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(click_zone, LV_OBJ_FLAG_CLICKABLE);

        // Scroll Panel1 to top so Speedometer is the first item in focus
        lv_obj_scroll_to_y(ui_Panel1, 0, LV_ANIM_OFF);

        // Reduce scroll throw so one swipe = one item  (default is 10 = too much momentum)
        indev_drv.scroll_throw = 3;

        // The click_zone (inside sel_bar) dispatches to whichever button is snapped to center.
        lv_obj_add_event_cb(click_zone, [](lv_event_t * e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            if (ignore_until_lift) return;
            if (!ui_Panel1) return;

            // ch_y is relative to the content area (after pad_top), so item i
            // is perfectly centered in the panel when scroll_y == ch_y.
            // Match condition: abs(ch_y - scroll_y) < 45
            lv_coord_t scroll_y = lv_obj_get_scroll_y(ui_Panel1);
            uint32_t   cnt      = lv_obj_get_child_cnt(ui_Panel1);
            int sel = -1;
            for (uint32_t i = 0; i < cnt; i++) {
                lv_obj_t * ch    = lv_obj_get_child(ui_Panel1, i);
                lv_coord_t ch_y  = lv_obj_get_y(ch);
                Serial.printf("[SEL] child=%d ch_y=%d scroll_y=%d diff=%d\n",
                              i, (int)ch_y, (int)scroll_y, (int)abs(ch_y - scroll_y));
                if (abs(ch_y - scroll_y) < 45) { sel = (int)i; break; }
            }
            Serial.printf("[SEL] Dispatching index: %d\n", sel);
            // Button order in Panel1 (ui_uilauncher_screen_init order):
            // 0=Speedometer 1=Inclinometer 2=Gauges 3=ImageFrame 4=Games 5=Settings 6=About
            switch (sel) {
                case 0: 
                    if (default_speedometer == 1) {
                        _ui_screen_change(&ui_godzillaspeedometer, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_godzillaspeedometer_screen_init);
                    } else {
                        _ui_screen_change(&ui_uispeedometer,  LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_uispeedometer_screen_init);
                    }
                    break;
                case 1: _ui_screen_change(&ui_uiinclinometer, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_uiinclinometer_screen_init); break;
                case 2: _ui_screen_change(&ui_uigauge,        LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_uigauge_screen_init);        break;
                case 3: app_imageframe(NULL); break;
                case 4: build_rom_menu();     break;
                case 5: build_settings_screen(); break;
                case 6: build_about_screen(); break;
                default: Serial.printf("[SEL] No match for index %d\n", sel); break;
            }
        }, LV_EVENT_ALL, NULL);
    }
    if (reboot_into_games == 1) { reboot_into_games = 0; build_rom_menu(); }
    if (ui_uiScreenGame) lv_obj_add_event_cb(ui_uiScreenGame, app_dino_jump_trigger, LV_EVENT_PRESSED, NULL);

    // Spawn core-0 dedicated OBD background telemetry parsing engine
    xTaskCreatePinnedToCore(obdBackgroundWorker, "OBD_Task", 8192, NULL, 1, NULL, 0);

    // Boot into default speedometer dynamically without conflicting with ui.c animations
    if (default_speedometer == 1) {
        _ui_screen_change(&ui_godzillaspeedometer, LV_SCR_LOAD_ANIM_FADE_ON, 800, 0, &ui_godzillaspeedometer_screen_init);
    } else {
        lv_scr_load_anim(ui_uispeedometer, LV_SCR_LOAD_ANIM_FADE_ON, 800, 0, true);
    }

    // First-run onboarding: if no WiFi networks are saved yet, open the Wi-Fi
    // setup overlay with the hotspot already ON so the user can configure WiFi.
    lv_timer_t * onboarding = lv_timer_create([](lv_timer_t * t) {
        char ssids[8][33];
        bool no_creds = (ota_list_networks(ssids, 8) == 0);
        // Empty SD card? (browser-flash leaves NVS creds intact, so also key off
        // missing SD content — trailmaster.bmp is part of the standard SD set.)
        FILE* fc = fopen("/sd_card/trailmaster.bmp", "r");
        bool sd_missing = (fc == NULL);
        if (fc) fclose(fc);
        if (no_creds || sd_missing) {
            pf_autostart_wifi = true;
            pf_show_upload_overlay();   // overlay sits over the speedometer; X reveals it
        }
        lv_timer_del(t);
    }, 2000, NULL);
    (void)onboarding;
}

void loop() {
    static uint32_t last_tick = 0;
    lv_tick_inc(millis() - last_tick);
    last_tick = millis();

    if (launcher_pending) { launcher_pending = false; switch_to_launcher(); }

    if (currentMode != MODE_EMULATOR) {
        lv_timer_handler();
        
        if (game_selected) {
            game_selected = false;
            currentMode = MODE_EMULATOR;
            Serial.println("[LAUNCH] Pausing UI... Entering Emulator...");
            
            // Note: We no longer nuclear-purge the UI screens because the S3 has 
            // 8MB of PSRAM and our new image loaders use it, leaving plenty of standard heap!
            
            // STOP LVGL TIMERS
            lv_timer_enable(false);
            lv_timer_handler(); 
            
            amoled.fillScreen(AMOLED_COLOR_BLACK);
            delay(1000); // Breathe deeply
            
            Serial.printf("[DEBUG] System Ready. ROM: %s\n", selected_rom_path);
            Serial.printf("[DEBUG] Heap: %u, PSRAM: %u\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            
            if (!NesEngine::loadROM(selected_rom_path)) {
                Serial.println("[DEBUG] NesEngine::loadROM returned FALSE");
                lv_timer_enable(true);
                build_rom_menu();
            }
        }
        
        static lv_obj_t * prev_act_scr = NULL;
        lv_obj_t * act_scr = lv_scr_act();
        if (act_scr != prev_act_scr) {
            // LEAVING inclinometer: kill IMU to stop I2C bus flooding
            if (prev_act_scr == ui_uiinclinometer && imu_ready) {
                qmi8658_enableSensors(QMI8658_DISABLE_ALL);
                imu_ready = false;
                Serial.println("[IMU] Sensors DISABLED - screen exited");
            }
            prev_act_scr = act_scr;
            screen_load_time = millis();
        }

        if (act_scr == ui_uiinclinometer) {
            if (currentMode != MODE_INCLINOMETER) load_inclinometer_mode(NULL);
            update_screen_inclinometer(); 
        }
        else if (act_scr == ui_uiScreenGame) update_screen_game(); 
        else if (act_scr == ui_uispeedometer || act_scr == ui_uigauge || act_scr == ui_godzillaspeedometer) update_screen_ui();

        extern bool wifi_ap_running;
        if (currentMode == MODE_PHOTOFRAME || wifi_ap_running) photoframe_loop_handler();
    } else {
        delay(50); // Just yield while emulator runs
        
        if (!NesEngine::is_running) {
            extern void force_touch_reset();
            force_touch_reset(); // Flush residual swipe-down touches!
            lv_timer_enable(true);
            amoled.fillScreen(AMOLED_COLOR_BLACK);
            build_rom_menu();
            currentMode = MODE_NES_BROWSER;
        }
    }
}

// --- EXTERNAL C TRIGGERS ---
extern "C" {
    void app_open_games_menu(lv_event_t * e); 
    void app_start_dino_game(lv_event_t * e) { build_rom_menu(); }
    void load_ui_mode(lv_event_t * e) { currentMode = MODE_UI; }
    void stop_dino_game(lv_event_t * e) { 
        stop_all_games();
        currentMode = MODE_UI; 
    }
    void app_settings(lv_event_t * e) { build_settings_screen(); }
    void app_zero_inclinometer(lv_event_t * e) { imu_pitch_offset = raw_imu_pitch; imu_roll_offset = raw_imu_roll; }
    void app_dino_jump_trigger(lv_event_t * e) { 
        if (currentMode == MODE_GAME) {
            if (activeGame == GAME_DINO) {
                extern bool dino_ready;
                extern float enemy_x;
                if (is_game_over) {
                    // REVIVE: Reset and Start over
                    is_game_over = false; reset_obstacles(); 
                    game_timer = millis(); last_restart_time = millis();
                    return;
                }
                if (dino_ready) {
                    dino_ready = false;
                    is_jumping = true;
                    jump_vel = -8.5;
                    last_restart_time = millis();
                    enemy_x = 240 + random(100, 200);
                    return;
                }
                if (!is_jumping && millis() - last_restart_time > 200) { is_jumping = true; jump_vel = -8.5; } 
            } else if (activeGame == GAME_FLAPPY) {
                if (is_game_over) {
                    // REVIVE: Reset and Start over
                    is_game_over = false; reset_flappy_game(); 
                    game_timer = millis(); last_restart_time = millis();
                    return;
                }
                flappy_flap();
            }
        }
    }
    void load_inclinometer_mode(lv_event_t * e) { 
        delay(200); 
        currentMode = MODE_INCLINOMETER; 
        imu_settle_count = 30; // Force skip 30 frames to avoid startup spin surge
        auto_reset_triggered = false; // ARM 2.5s auto-zero trigger
        screen_load_time = millis(); // CRITICAL: stamp NOW so 2500ms counts from IMU init, not boot
        
        if (!imu_ready) { 
            if(qmi8658_init()) { 
                qmi8658_config_reg(0); 
                qmi8658_enableSensors(QMI8658_ACCGYR_ENABLE); 
                imu_ready = true; 
                last_imu_time = millis(); 
            } 
        }

        // Ensure Tap-To-Reset and Footer functionality is added to the loaded screen
        if (ui_uiinclinometer) {
            lv_obj_add_flag(ui_uiinclinometer, LV_OBJ_FLAG_CLICKABLE);
            // Add click event (safely once per load is fine)
            lv_obj_add_event_cb(ui_uiinclinometer, [](lv_event_t * ev) {
                if (lv_event_get_code(ev) == LV_EVENT_CLICKED) {
                    imu_pitch_offset = raw_imu_pitch;
                    imu_roll_offset  = raw_imu_roll;
                    Serial.println("[IMU] Tapped to Reset Angles");
                }
            }, LV_EVENT_CLICKED, NULL);

            // Add footer label once
            static lv_obj_t * tap_hint = NULL;
            if (!tap_hint || lv_obj_get_parent(tap_hint) != ui_uiinclinometer) {
                tap_hint = lv_label_create(ui_uiinclinometer);
                lv_label_set_text(tap_hint, "Tap to reset");
                lv_obj_set_style_text_font(tap_hint, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(tap_hint, lv_color_hex(0x777777), 0);
                lv_obj_align(tap_hint, LV_ALIGN_BOTTOM_MID, 0, -15);
            }
            // Build roll arc gauges once per screen lifetime
            static bool arcs_built = false;
            if (!arcs_built) {
                build_roll_arcs(ui_uiinclinometer);
                arcs_built = true;
            }
        }
    }
    void app_imageframe(lv_event_t * e) { delay(200); currentMode = MODE_PHOTOFRAME; switch_to_photoframe(); }
    void app_about(lv_event_t * e) { build_about_screen(); }
    void switch_to_launcher() {
        if (ui_uilauncher) {
            lv_obj_t * current = lv_scr_act();
            if (current != ui_uilauncher) last_screen_before_launcher = current;
            lv_scr_load_anim(ui_uilauncher, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
            currentMode = MODE_UI;

            if (use_grid_launcher) {
                if (ui_Panel1) lv_obj_add_flag(ui_Panel1, LV_OBJ_FLAG_HIDDEN);
                if (sel_bar) lv_obj_add_flag(sel_bar, LV_OBJ_FLAG_HIDDEN);
                build_grid_launcher();
            } else {
                if (ui_Panel1) lv_obj_clear_flag(ui_Panel1, LV_OBJ_FLAG_HIDDEN);
                if (sel_bar) lv_obj_clear_flag(sel_bar, LV_OBJ_FLAG_HIDDEN);
                // Reset Panel1 scroll AFTER the screen is loaded so layout has occurred.
                if (ui_Panel1) lv_obj_scroll_to_y(ui_Panel1, 0, LV_ANIM_ON);
            }
            // Removed force_full_ui_redraw(ui_uilauncher); to prevent breaking the fade animation with a forced full refresh
        }
    }
}

// ─── GRID LAUNCHER ────────────────────────────────────────────────────────

// Shared with the simulator (sim/) — see grid_launcher_ui.h. Editing that
// file changes what BOTH the device and the sim render; there's only one copy.
#include "grid_launcher_ui.h"
