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
#include "convoy_ui.h"   // shared convoy/tracker radar (same header the sim renders)
#include "convoy_link.h" // BLE client to the co-located Meshtastic T-Beam (NimBLE 2.x)
#include "convoy_net.h"  // BLE peripheral: phone (Web Bluetooth) pushes the convoy roster
#include "convoy_wifi.h" // WiFi STA: board pulls the roster from Firebase itself
#include "convoy_source_ui.h" // source picker: choose Meshtastic (scan) or phone at runtime
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

// Gauge arc zones (engine load / coolant temp): amber/hot/redline, same
// palette as the speedometer's RPM zones in screen_ui.h, just keyed by
// percent-of-range instead of RPM.
lv_color_t get_dynamic_color(float percent) {
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    if (percent < 0.40f) return lv_color_hex(0xFFB020); // amber
    if (percent < 0.80f) return lv_color_hex(0xFFC54D); // hot
    return lv_color_hex(0xFF3B1D);                      // redline
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
bool tracker_enabled = true; // Settings toggle: show the Convoy Tracker launcher tile
// Tracker "radio mode": while the Tracker is open it OWNS the radio — WiFi is
// powered down (frees internal RAM/DMA) and BLE runs instead. WiFi+BLE+the AMOLED
// don't fit in internal RAM together (BLE corrupts the display). The OBD worker
// watches convoy_radio_mode and releases WiFi; convoy_obd_released acks it.
volatile bool convoy_radio_mode = false;
volatile bool convoy_obd_released = false;
// ...except for the WiFi convoy source, which wants the WiFi STACK, not the raw
// radio: it joins the owner's hotspot itself. Set alongside convoy_radio_mode to
// tell the worker "stop touching WiFi" WITHOUT powering it down — no NimBLE is
// coming, so the internal-RAM cliff that WIFI_OFF exists to dodge doesn't apply,
// and a needless down/up would just cost seconds. See CONVOY_WIFI_PLAN.md.
volatile bool convoy_wifi_mode = false;
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
static void convoy_cancel_loads(void);   // defined with the convoy loaders, used by loop()
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
        // Tracker radio mode: release WiFi entirely so BLE can own the radio.
        if (convoy_radio_mode) {
            // The Tracker wants the radio. This worker is the ONLY task that touches
            // WiFi, so the whole teardown happens HERE and the ack means "WiFi is
            // off" — not "I asked it to stop". convoyLinkTask used to power WiFi down
            // itself, from a different task, after a fixed 4s wait, while this worker
            // could still be blocked inside a 10s WiFi.begin(). That handover race is
            // what left NimBLE initialising at ~46KB free.
            if (!convoy_obd_released) {
                if (ota_st->state != OTA_IDLE) {
                    // Never pull WiFi out from under an update. Withholding the ack
                    // makes the Tracker report "Radio unavailable" instead.
                    vTaskDelay(pdMS_TO_TICKS(250));
                    continue;
                }
                if (client.connected()) { client.stop(); }
                WiFi.disconnect(true, true);
                if (wifi_ap_running) { WiFi.softAPdisconnect(true); }
                if (!convoy_wifi_mode) {
                    WiFi.mode(WIFI_OFF);
                    vTaskDelay(pdMS_TO_TICKS(200));   // let the stack settle before NimBLE
                }
                Serial.printf("[OBD] WiFi released for Tracker%s (internal RAM=%u)\n",
                              convoy_wifi_mode ? " (STA kept up)" : "",
                              heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
                convoy_obd_released = true;       // ack: the radio is genuinely free
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        convoy_obd_released = false;
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
                // Bail out the moment the Tracker asks for the radio. Sitting out the
                // full 10s here is the other half of the handover race: the request
                // would go unseen long past the point convoyLinkTask gave up waiting.
                if (convoy_radio_mode) {
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

static void make_settings_section_header(lv_obj_t * parent, const char * text) {
    lv_obj_t * h = lv_label_create(parent);
    lv_label_set_text(h, text);
    lv_obj_set_width(h, 430);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_left(h, 70, 0); // offset from round-screen left curvature
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_pad_top(h, 6, 0);
}

// ─── Boot image: which image is remembered right now, regardless of on/off
// state — used only to show a filename hint next to the toggle below. The
// actual picking happens via the existing "SET AS BOOTLOADER" long-press
// button in the Image Frame carousel (PhotoFrameApp.cpp); this toggle only
// turns that already-chosen image on/off, it never lets you pick one here.
static String get_remembered_boot_image_name() {
    FILE* f = fopen("/sd_card/boot_img.txt", "r");
    if (!f) f = fopen("/sd_card/boot_img.txt.disabled", "r");
    if (!f) return String();
    char buf[160] = {0};
    if (fgets(buf, sizeof(buf), f)) buf[strcspn(buf, "\r\n")] = 0;
    fclose(f);
    const char * slash = strrchr(buf, '/');
    return String(slash ? slash + 1 : buf);
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
    lv_obj_clear_flag(scroll_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // no coast animation after lift
    lv_obj_clear_flag(scroll_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);   // no rubber-band overscroll
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scroll_cont, 8, 0);
    lv_obj_set_style_pad_bottom(scroll_cont, 60, 0); // extra clearance for round-screen bottom curve
    lv_obj_set_style_pad_gap(scroll_cont, 10, 0);

    // Use scroll_cont as parent for all rows
    lv_obj_t * scr_rows = scroll_cont;

    make_settings_section_header(scr_rows, "DISPLAY");

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
    lv_obj_set_style_text_font(lbl_bright, &lv_font_montserrat_24, 0);
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
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_20, 0);
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

    make_settings_section_header(scr_rows, "CONNECTIVITY");

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
    lv_obj_set_style_text_font(lbl_wifi_s, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_wifi_s, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_wifi_s, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * arrow_wifi = lv_label_create(row_wifi);
    lv_label_set_text(arrow_wifi, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow_wifi, lv_color_hex(0x888888), 0);
    lv_obj_align(arrow_wifi, LV_ALIGN_RIGHT_MID, -18, 0);

    lv_obj_add_event_cb(row_wifi, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) pf_show_upload_overlay();
    }, LV_EVENT_ALL, NULL);

    make_settings_section_header(scr_rows, "MODES");

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
    lv_obj_set_style_text_font(lbl_logo, &lv_font_montserrat_24, 0);
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
    lv_obj_set_style_text_font(lbl_grid, &lv_font_montserrat_24, 0);
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

    // ─── ROW 1.8: Convoy Tracker Toggle ──────────────────────────────────────
    lv_obj_t * row_trk = lv_obj_create(scr_rows);
    lv_obj_set_size(row_trk, 430, 80);
    lv_obj_set_style_bg_color(row_trk, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_trk, 255, 0);
    lv_obj_set_style_border_width(row_trk, 1, 0);
    lv_obj_set_style_border_color(row_trk, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_trk, 12, 0);
    lv_obj_clear_flag(row_trk, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_trk = lv_label_create(row_trk);
    lv_label_set_text(lbl_trk, "Convoy Tracker");
    lv_obj_set_style_text_font(lbl_trk, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_trk, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_trk, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * sw_trk = lv_switch_create(row_trk);
    lv_obj_align(sw_trk, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_size(sw_trk, 60, 30);
    lv_obj_set_style_bg_color(sw_trk, lv_color_hex(0xFF6A00), LV_PART_INDICATOR | LV_STATE_CHECKED);

    if (tracker_enabled) lv_obj_add_state(sw_trk, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw_trk, LV_STATE_CHECKED);

    lv_obj_add_event_cb(sw_trk, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        lv_obj_t * obj = lv_event_get_target(e);
        tracker_enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
        Preferences p;
        p.begin("hellojimny", false);
        p.putInt("tracker_en", tracker_enabled ? 1 : 0);
        p.end();
        Serial.printf("[SETTINGS] Convoy Tracker set to %d\n", tracker_enabled);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    make_settings_section_header(scr_rows, "BOOT IMAGE");

    // ─── ROW 3: Boot duration (pre-created so Row 2's switch can show/hide
    // it — only relevant once a custom boot image is actually toggled on).
    lv_obj_t * row3 = lv_obj_create(scr_rows);
    lv_obj_set_size(row3, 430, 80);
    lv_obj_set_style_bg_color(row3, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row3, 255, 0);
    lv_obj_set_style_border_width(row3, 1, 0);
    lv_obj_set_style_border_color(row3, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row3, 12, 0);
    lv_obj_clear_flag(row3, LV_OBJ_FLAG_SCROLLABLE);

    // ─── ROW 2: Boot image on/off — picking WHICH image happens via "SET AS
    // BOOTLOADER" on a long-press in Image Frame (PhotoFrameApp.cpp); this is
    // just the on/off switch for whatever's already been chosen there.
    lv_obj_t * row2 = lv_obj_create(scr_rows);
    lv_obj_set_size(row2, 430, 80);
    lv_obj_set_style_bg_color(row2, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row2, 255, 0);
    lv_obj_set_style_border_width(row2, 1, 0);
    lv_obj_set_style_border_color(row2, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row2, 12, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    // row3 was created first (so this callback can reference it) but should
    // appear below row2 — push it to the end of the flex flow.
    lv_obj_move_foreground(row3);

    lv_obj_t * lbl_boot = lv_label_create(row2);
    lv_label_set_text(lbl_boot, "Boot image");
    lv_obj_set_style_text_font(lbl_boot, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_boot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_boot, LV_ALIGN_LEFT_MID, 14, 0);

    String remembered_name = get_remembered_boot_image_name();
    lv_obj_t * lbl_boot_sub = lv_label_create(row2);
    lv_label_set_long_mode(lbl_boot_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_boot_sub, 110);
    lv_obj_set_style_text_font(lbl_boot_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_boot_sub, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_boot_sub, LV_ALIGN_RIGHT_MID, -75, 0);

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
        lv_label_set_text(lbl_boot_sub, remembered_name.c_str());
        lv_obj_clear_flag(row3, LV_OBJ_FLAG_HIDDEN); // Show boot duration setting
    } else if (off_exists) {
        lv_obj_clear_state(sw_boot, LV_STATE_CHECKED);
        lv_label_set_text(lbl_boot_sub, remembered_name.c_str());
        lv_obj_add_flag(row3, LV_OBJ_FLAG_HIDDEN); // Hide boot duration setting
    } else {
        // Neither file exists — nothing chosen yet via Image Frame's long-press menu.
        lv_obj_add_state(sw_boot, LV_STATE_DISABLED);
        lv_label_set_text(lbl_boot_sub, "Not set");
        lv_obj_add_flag(row3, LV_OBJ_FLAG_HIDDEN); // Hide boot duration setting
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

    lv_obj_t * lbl_boot_time = lv_label_create(row3);
    lv_label_set_text(lbl_boot_time, "Boot duration");
    lv_obj_set_style_text_font(lbl_boot_time, &lv_font_montserrat_24, 0);
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
    lv_obj_set_style_text_font(lbl_time_val, &lv_font_montserrat_20, 0);
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
        
        // SWIPE DOWN TO LAUNCHER — only when swipe STARTS in the top ~80px
        // (above where any scroll container sits). Without this guard the
        // downward-scroll gesture in Settings / any scrollable list also
        // triggers launcher navigation mid-list.
        if (lv_scr_act() != ui_uilauncher && touch_start_y < 80 && (y - touch_start_y > 100)) {
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
        
        // SWIPE UP: screenshot gesture removed — lv_snapshot_take() blocks
        // 200-500ms (allocates ~434KB + full off-screen render) and was
        // firing on the first swipe of any gesture, causing the noticeable
        // lag on Settings scroll and launcher navigation. Re-bind to a
        // hardware button if you want this feature back.
        
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
        tracker_enabled = p.getInt("tracker_en", 1) ? true : false; // Default ON
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
    // NOTE: the Convoy BLE link is NOT started here — it spins up only when the
    // user opens the Tracker screen (convoy_open_screen) and is torn down on exit,
    // so BLE never competes with WiFi/OBD for internal RAM during normal use.

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
            // LEAVING the tracker (radar OR either picker page): tear down the BLE
            // link to free RAM for WiFi/OBD. Moving between radar ⇄ picker stays in.
            bool prev_cvy = (prev_act_scr == convoy_screen ||
                             prev_act_scr == convoy_src_screen ||
                             prev_act_scr == convoy_src_scan_screen);
            bool act_cvy  = (act_scr == convoy_screen ||
                             act_scr == convoy_src_screen ||
                             act_scr == convoy_src_scan_screen);
            // Cancel pending loads BEFORE stopping the link: a load armed inside the
            // Tracker would otherwise still fire (up to 4s later) and yank a rebuilt
            // convoy screen back over the launcher.
            if (prev_cvy && !act_cvy) { convoy_cancel_loads(); convoy_link_stop(); }
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

// ─── TRACKER (convoy radar) ───────────────────────────────────────────────
// PROTOTYPE: driven by mock data so it renders on the device today. To switch
// to the real Meshtastic T-Beam feed, add NimBLE-Arduino, then in setup()
//   #include "convoy_link.h"  →  convoy_link_begin(); + a FreeRTOS task calling
//   convoy_link_loop();  and delete convoy_mock_tick (convoy_link feeds the same
//   convoy_set_self/heading/car). convoy_refresh() must stay on this UI thread.
// BLE link to the T-Beam runs ONLY while the Tracker screen is open, so it never
// fights WiFi/OBD for internal RAM during normal use. Started in
// convoy_open_screen(), stopped when the user leaves the Tracker (loop()).
static TaskHandle_t convoyTaskHandle = NULL;
static volatile bool convoyRun = false;

// ── Runtime convoy source (replaces the old compile-time CONVOY_SOURCE_NET) ───
// The user picks the source on-device (convoy_source_ui.h); the BLE task brings
// up the matching NimBLE role and switches when the selection changes.
typedef enum { CVS_NONE, CVS_SCAN, CVS_MESH, CVS_PHONE } cvs_source_t;
static volatile cvs_source_t convoy_src_sel = CVS_NONE;   // what the UI wants
static volatile bool convoy_role_ready = false;           // task set: the role is up
static volatile bool convoy_radio_failed = false;         // task set: BLE bring-up failed
static char convoy_sel_mac[18] = {0};                     // chosen T-Beam MAC (mesh)
static const char * CONVOY_PHONE_URL = "tinyurl.com/trailmstr";

// SPIKE (2026-07-28): build_opt.h (flags-only file — NO comments allowed there,
// it is a raw GCC response file) sets CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL,
// which moves NimBLE *host* allocations to PSRAM instead of internal RAM. The
// ESP-IDF BT *controller* buffers stay internal, hence the heap logging below.
// If enough internal RAM stays free for WiFi, BLE and WiFi/OBD coexist and the
// whole radio-handover handshake can be deleted.
//
// With that in place, try keeping WiFi UP. The handover below is the suspected
// crash: convoyLinkTask
// waits only 4s for the OBD worker's ack, then powers the WiFi stack down from
// a DIFFERENT task — while that worker may still be blocked inside WiFi.begin()
// (up to ~10s) or a socket read. Set to 0 to restore the old handover.
// SPIKE RESULT (2026-07-28): 1 = FAILS. With WiFi/AP up the BT *controller*
// cannot get its buffers: "BLE_INIT: Malloc failed / esp_bt_controller_init -4"
// at 21-47KB free internal. PSRAM host alloc does not help — the failure is
// controller-side. Left at 0; BLE and WiFi cannot coexist on this build.
#define CONVOY_KEEP_WIFI 0

// Phase 1 (CONVOY_WIFI_PLAN.md). 1 = the Tracker does NOT bring up BLE at all:
// it takes WiFi instead, joins a saved hotspot and streams the convoy roster
// from Firebase for as long as the Tracker is open. Set back to 0 for the BLE
// sources (mesh / phone relay), which cannot run at the same time — the BT
// controller cannot get its buffers with the WiFi stack up.
//
// Still a compile-time switch rather than a picker entry: Phase 2 adds CVS_CLOUD
// to convoy_source_ui.h once this path has run on the road.
// The room is not compiled in: an unlinked board announces itself to Firebase
// and waits to be picked in the convoy app's Connect list.
#define CONVOY_WIFI_SPIKE 1

static void convoyLinkTask(void * arg) {
    (void)arg;
#if CONVOY_WIFI_SPIKE
    // Same ownership rule as the BLE path: ASK the OBD worker, never touch WiFi
    // from here until it acks. convoy_wifi_mode makes that ack mean "I've let go
    // of WiFi" instead of "WiFi is powered down".
    convoy_wifi_mode  = true;
    convoy_radio_mode = true;
    const uint32_t cvw_deadline = millis() + 12000;   // > the worker's ~10s connect
    while (!convoy_obd_released && millis() < cvw_deadline) vTaskDelay(pdMS_TO_TICKS(50));
    if (!convoy_obd_released) {
        Serial.println("[CVW] WiFi handover NOT acked — leaving the radio to OBD");
        convoy_radio_failed = true;
        convoy_role_ready   = true;
    } else {
        const bool ok = convoy_wifi_begin();
        convoy_radio_failed = !ok;
        convoy_role_ready   = true;   // UI may load the radar (up, or failed)
        while (convoyRun && ok) {
            convoy_wifi_loop();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        convoy_wifi_end();
    }
    convoy_radio_mode = false;     // hand the radio back to OBD
    convoy_wifi_mode  = false;
    convoyRun         = false;
    convoyTaskHandle  = NULL;
    vTaskDelete(NULL);
    return;
#endif
#if CONVOY_KEEP_WIFI
    Serial.printf("[CVY] keep-WiFi spike: internal=%u psram=%u wifi_status=%d — BLE task up\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (int)WiFi.status());
#else
    // ── Acquire the radio. The OBD worker OWNS WiFi; this task only asks and waits
    // for its ack. It must never call a WiFi API itself — doing that from here while
    // the worker could still be inside a blocking WiFi.begin() is precisely what
    // corrupted the handover and left NimBLE initialising at ~46KB free.
    //
    // The ack is now MANDATORY. If it never arrives (an OTA is running, or the worker
    // is wedged) we do NOT force the radio down: report it unavailable and exit,
    // leaving WiFi/OBD exactly as they were. A visible "Radio unavailable / Tap to
    // retry" beats half-owning the radio.
    convoy_radio_mode = true;
    const uint32_t ack_deadline = millis() + 12000;   // > the worker's ~10s connect
    while (!convoy_obd_released && millis() < ack_deadline) vTaskDelay(pdMS_TO_TICKS(50));
    if (!convoy_obd_released) {
        Serial.println("[CVY] WiFi handover NOT acked — leaving the radio to OBD");
        convoy_radio_mode   = false;
        convoy_radio_failed = true;
        convoy_role_ready   = true;   // release the UI's wait-for-role loader at once
        convoyRun           = false;
        convoyTaskHandle    = NULL;
        vTaskDelete(NULL);
        return;
    }
    Serial.printf("[CVY] radio acquired (OBD acked); internal RAM=%u — BLE task up\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif

    cvs_source_t running = CVS_NONE;              // which BLE role is actually up
    while (convoyRun) {
        cvs_source_t want = convoy_src_sel;
        if (want != running) {
            bool was_c  = (running == CVS_SCAN || running == CVS_MESH);
            bool want_c = (want == CVS_SCAN   || want == CVS_MESH);
            if (was_c && want_c) {
                // central → central (scan ⇄ mesh): keep NimBLE up, just retarget.
                if (want == CVS_SCAN) convoy_link_rescan();
                else                  convoy_link_connect_mac(convoy_sel_mac);
            } else {
                // role-class change or to/from NONE: full teardown then bring-up.
                if (was_c) convoy_link_end();
                else if (running == CVS_PHONE) convoy_net_end();
                // Settle before bringing up the new role: lets the prior NimBLE
                // stack fully tear down AND the display's full-redraw DMA finish,
                // so the new NimBLE init doesn't collide with either.
                vTaskDelay(pdMS_TO_TICKS(400));
                // Bring-up can FAIL (BT controller out of internal RAM). Record it
                // instead of ploughing on into NimBLE calls that would assert and
                // reboot — the UI shows "Radio unavailable".
                bool ok = true;
                if (want == CVS_SCAN)       ok = convoy_link_begin_scan();
                else if (want == CVS_MESH)  { ok = convoy_link_begin_scan(); if (ok) convoy_link_connect_mac(convoy_sel_mac); }
                else if (want == CVS_PHONE) ok = convoy_net_begin();
                convoy_radio_failed = !ok;
            }
            running = want;
            convoy_role_ready = true;      // UI may now load the radar (init done or failed)
            Serial.printf("[CVY] role -> %d  internal=%u psram=%u wifi_status=%d\n",
                          (int)running, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (int)WiFi.status());
        }
        if (running == CVS_SCAN || running == CVS_MESH) convoy_link_loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (running == CVS_SCAN || running == CVS_MESH) convoy_link_end();
    else if (running == CVS_PHONE) convoy_net_end();

    // ── Release the radio back to WiFi/OBD (worker re-inits WiFi on its own).
    convoy_radio_mode = false;   // no-op under CONVOY_KEEP_WIFI (never set)
    Serial.printf("[CVY] BLE task down; internal=%u wifi_status=%d\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)WiFi.status());
    convoyTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Master switch for the BLE link. ON: the Tracker enters "radio mode" — WiFi is
// powered down first (convoyLinkTask), freeing the internal RAM that NimBLE
// otherwise collided with (which corrupted the AMOLED). BLE runs while the
// Tracker is open; leaving hands the radio back to WiFi/OBD.
#define CONVOY_BLE_ENABLE 1

static void convoy_link_start(void) {
#if CONVOY_BLE_ENABLE
    if (convoyTaskHandle || !tracker_enabled) return;
    convoyRun = true;
    xTaskCreatePinnedToCore(convoyLinkTask, "Convoy_BLE", 12288, NULL, 1, &convoyTaskHandle, 0);
#endif
}
static void convoy_link_stop(void) { convoyRun = false; }   // task self-cleans

// Clean screen loader for the picker flow. Two jobs, both to avoid the BLE task's
// NimBLE role switch (which grabs/frees internal DMA RAM) colliding with the
// display transition — that collision corrupts the AMOLED flush and, on the
// heavier central→peripheral switch, crashes:
//   1. defer the load OUT of the LVGL event, then force a full panel redraw;
//   2. apply the pending source change ONLY AFTER the redraw settles, so the BLE
//      switch never overlaps the heavy render.
static lv_obj_t *  convoy_pending_scr = NULL;
static cvs_source_t convoy_pending_sel = CVS_NONE;
static bool        convoy_apply_sel   = false;
static bool        convoy_load_busy   = false;  // a load (either kind) is in flight
static lv_obj_t *  convoy_ready_scr   = NULL;   // target of a wait-for-role load
static lv_timer_t * convoy_defer_timer = NULL;  // in-flight convoy_load_clean
static lv_timer_t * convoy_ready_timer = NULL;  // in-flight convoy_load_when_ready
static lv_timer_t * convoy_autoarm_timer = NULL;      // pending staggered auto-arm
static cvs_source_t convoy_autoarm_sel   = CVS_NONE;  // source it will arm
// Abandon any in-flight screen load. Both loaders are lv_timers that fire LATER —
// the wait-for-role one up to 4s later — and then call lv_scr_load() on a convoy
// screen. If the user has left the Tracker by then, that screen is no longer the
// one on display and convoy_open_screen() rebuilds it on the next visit, so a late
// fire dereferences a stale object: the LoadProhibited panic inside lv_obj_set_pos
// when swiping down to the launcher. Cancel them on the way out.
static void convoy_cancel_loads(void) {
    if (convoy_defer_timer)   { lv_timer_del(convoy_defer_timer);   convoy_defer_timer   = NULL; }
    if (convoy_ready_timer)   { lv_timer_del(convoy_ready_timer);   convoy_ready_timer   = NULL; }
    if (convoy_autoarm_timer) { lv_timer_del(convoy_autoarm_timer); convoy_autoarm_timer = NULL; }
    convoy_autoarm_sel = CVS_NONE;   // left before the stagger elapsed — don't arm
    convoy_pending_scr = NULL;
    convoy_ready_scr   = NULL;
    convoy_apply_sel   = false;
    convoy_load_busy   = false;
}
// True only while a convoy screen is the one actually showing.
//
// Loading a screen from a timer when we are NOT in the Tracker is what panics: our
// screens are build-once and never freed, so the bad pointer is not ours — it is
// LVGL's internal d->scr_to_load. lv_scr_load() re-enters lv_scr_load_anim(), which,
// if another screen-load ANIMATION is still in flight, "finishes" it immediately via
//     lv_obj_set_pos(d->scr_to_load, 0, 0)      (lv_disp.c:236)
// and can lv_obj_del(act_scr) when that load asked for auto_del — which the
// speedometer's 800ms fade does. So a late convoy load landing mid-transition
// dereferences another screen's half-finished animation state.
static bool convoy_ui_active(void) {
    lv_obj_t * a = lv_scr_act();
    return a && (a == convoy_screen || a == convoy_src_screen || a == convoy_src_scan_screen);
}
static void convoy_deferred_load_cb(lv_timer_t * t) {
    convoy_defer_timer = NULL;
    if (!convoy_ui_active()) {          // navigated away mid-transition — abandon
        convoy_pending_scr = NULL; convoy_apply_sel = false; convoy_load_busy = false;
        lv_timer_del(t);
        return;
    }
    if (convoy_pending_scr && lv_obj_is_valid(convoy_pending_scr)) {
        lv_scr_load(convoy_pending_scr);
        force_full_ui_redraw(convoy_pending_scr);
        convoy_pending_scr = NULL;
    }
    // A source change queued for a static screen (the scan page): apply it AFTER
    // the load so the NimBLE init runs while that page is idle, not mid-render.
    if (convoy_apply_sel) { convoy_role_ready = false; convoy_src_sel = convoy_pending_sel; convoy_apply_sel = false; }
    if (!convoy_ready_scr) convoy_load_busy = false;
    lv_timer_del(t);
}
static void convoy_load_clean(lv_obj_t * scr) {
    // Drop repeat/no-op taps: every load forces a full 466×466 redraw, and
    // stacking those (tapping ACQUIRING several times) piles up heavy renders.
    if (convoy_load_busy || lv_scr_act() == scr) return;
    convoy_load_busy   = true;
    convoy_pending_scr = scr;
    convoy_defer_timer = lv_timer_create(convoy_deferred_load_cb, 40, NULL);
}
// Queue a source change to apply after the next clean load (macro avoids the
// Arduino auto-prototype hoisting a cvs_source_t parameter above its typedef).
#define convoy_switch_source(s) do { convoy_pending_sel = (s); convoy_apply_sel = true; } while (0)

// Load a screen ONLY once the BLE task reports the pending role change is fully
// done (NimBLE init/deinit finished). Until then the currently shown (static,
// non-rendering) page stays up, so the heavy redraw never overlaps a radio
// transition — the collision that crashed (phone) and corrupted the flush
// (mesh). Used in BOTH directions: picker → radar and radar → picker. ~4s cap.
static uint32_t convoy_ready_t0 = 0;
static void convoy_ready_load_cb(lv_timer_t * t) {
    if (!convoy_role_ready && millis() - convoy_ready_t0 <= 4000) return;
    lv_obj_t * scr = convoy_ready_scr ? convoy_ready_scr : convoy_screen;
    convoy_ready_scr   = NULL;
    convoy_ready_timer = NULL;
    lv_timer_del(t);
    if (!convoy_ui_active()) {          // see convoy_deferred_load_cb — never load from here
        convoy_load_busy = false;       // once the user has navigated away
        return;
    }
    if (scr && lv_obj_is_valid(scr)) {
        lv_scr_load(scr);
        force_full_ui_redraw(scr);
    }
    convoy_load_busy = false;
}
static void convoy_load_when_ready(lv_obj_t * scr) {
    bool running = (convoy_ready_scr != NULL);
    convoy_ready_scr = scr;
    convoy_load_busy = true;
    if (!running) {
        convoy_ready_t0 = millis();
        convoy_ready_timer = lv_timer_create(convoy_ready_load_cb, 120, NULL);
    }
}
// Arm the radio. The BLE task is spawned only from here — either an explicit source
// tap, or the staggered auto-arm below.
static void convoy_radio_arm(void) {
    convoy_radio_failed = false;
    convoy_link_start();               // no-op if the task is already running
}

// ── Staggered auto-arm ───────────────────────────────────────────────────────
// Let the screen finish arriving BEFORE the radio comes up. Arming inline in
// convoy_open_screen() stacked NimBLE bring-up (and the WiFi handover it triggers)
// on top of the entry fade still animating — and a screen-load animation in flight
// is exactly when LVGL's loader is fragile: a load landing then runs the
// "finish the previous one immediately" path in lv_scr_load_anim(), which can
// lv_obj_del() the screen currently showing. Separating the two in time removes the
// overlap instead of guarding against it. The delay is free in practice: the link
// takes seconds to establish anyway, so nothing arrives later than it used to.
#define CONVOY_AUTOARM_DELAY_MS 700    // > the 250ms entry fade, with margin
static void convoy_autoarm_cb(lv_timer_t * t) {
    convoy_autoarm_timer = NULL;
    lv_timer_del(t);
    cvs_source_t want  = convoy_autoarm_sel;
    convoy_autoarm_sel = CVS_NONE;
    if (want == CVS_NONE) return;
    if (!convoy_ui_active()) return;   // user left during the stagger — stay off the radio
    convoy_radio_arm();
    convoy_role_ready = false;
    convoy_src_sel    = want;
}
// Macro, not a function: an Arduino auto-prototype taking cvs_source_t would be
// hoisted above the typedef (same reason convoy_switch_source is a macro).
#define convoy_autoarm_schedule(s) do {                                              \
    convoy_autoarm_sel = (s);                                                        \
    if (convoy_autoarm_timer) lv_timer_del(convoy_autoarm_timer);                    \
    convoy_autoarm_timer = lv_timer_create(convoy_autoarm_cb,                        \
                                           CONVOY_AUTOARM_DELAY_MS, NULL);           \
} while (0)
// Start a BLE role switch NOW (picker still shown → display idle), then load the
// radar once the role is up.
static void convoy_select_source_mesh(void) {
    convoy_radio_arm();
    convoy_role_ready = false; convoy_src_sel = CVS_MESH;
    convoy_load_when_ready(convoy_screen);
}
static void convoy_select_source_phone(void) {
    convoy_radio_arm();
    convoy_role_ready = false; convoy_src_sel = CVS_PHONE;
    convoy_load_when_ready(convoy_screen);
}

// ── Source-picker callbacks (UI thread) ──────────────────────────────────────
static int convoy_scan_rendered = -1;    // last device count rendered into the list

static void fw_src_scan(void) {           // tapped MESHTASTIC → start scanning
    convoy_scan_rendered = -1;
    convoy_radio_arm();                    // first explicit action that needs the radio
    convoy_switch_source(CVS_SCAN);        // applied after the scan page loads
}
static void fw_src_pick(int idx) {        // tapped a T-Beam → connect + persist
    convoy_scan_dev_t * arr; int n = convoy_link_scan_list(&arr);
    if (idx < 0 || idx >= n) return;
    strncpy(convoy_sel_mac, arr[idx].mac, sizeof(convoy_sel_mac) - 1);
    convoy_sel_mac[sizeof(convoy_sel_mac) - 1] = 0;
    Preferences p; p.begin("convoy", false);
    p.putString("tbeam_mac", convoy_sel_mac);
    p.putInt("source", 1);
    p.end();
    convoy_set_wait_text(NULL, NULL);
    convoy_set_self(0, 0, false);         // clear any stale position from the old source
    convoy_select_source_mesh();          // switch now (picker stays up), radar loads when ready
}
static void fw_src_phone(void) {          // tapped USE PHONE → advertise + persist
    Preferences p; p.begin("convoy", false);
    p.putInt("source", 2);
    p.end();
    convoy_set_wait_text("Connect via browser", CONVOY_PHONE_URL);
    convoy_set_self(0, 0, false);
    convoy_select_source_phone();         // switch now (picker stays up), radar loads when ready
}
// X on page 1 → back to the radar, keeping the chosen source. If the user backed
// out WITHOUT choosing one, the scan-only role is still up (CVS_SCAN): drop it to
// CVS_NONE so a NimBLE scan isn't left running behind the radar, and wait for that
// teardown before loading — an active scan colliding with the full-panel redraw is
// what rebooted the board when tapping ACQUIRING with no source selected.
static void fw_src_back(void) {
    if (convoy_src_sel == CVS_SCAN) {
        convoy_set_wait_text("No source selected", "Tap to choose");
        convoy_role_ready = false;
        convoy_src_sel    = CVS_NONE;
        convoy_load_when_ready(convoy_screen);
    } else {
        convoy_load_clean(convoy_screen);
    }
}

static void fw_open_picker(void) {        // radar panel tapped → open the picker
    if (convoy_load_busy) return;         // a transition is already in flight
    convoy_src_build_screen();
    convoy_src_on_scan        = fw_src_scan;
    convoy_src_on_pick_device = fw_src_pick;
    convoy_src_on_use_phone   = fw_src_phone;
    convoy_src_on_back        = fw_src_back;
    convoy_src_reset();
    if (convoy_src_sel == CVS_SCAN) {     // stray scan still running → stop it first
        convoy_role_ready = false;
        convoy_src_sel    = CVS_NONE;
        convoy_load_when_ready(convoy_src_screen);
    } else {
        convoy_load_clean(convoy_src_screen);
    }
}

// Render live BLE scan results into the picker list (UI thread; the task fills s_scan).
static void convoy_picker_tick(lv_timer_t * t) {
    (void)t;
    if (lv_scr_act() != convoy_src_scan_screen) return;
    convoy_scan_dev_t * arr; int n = convoy_link_scan_list(&arr);
    if (n != convoy_scan_rendered) {
        convoy_scan_rendered = n;
        convoy_src_clear_devices();
        for (int i = 0; i < n && i < CVSRC_MAX_DEV; i++)
            convoy_src_add_device(arr[i].name, arr[i].rssi);
        convoy_src_set_scanning(n == 0);
    }
}

static void convoy_mock_tick(lv_timer_t * t) {
    (void)t;
    if (lv_scr_act() != convoy_screen) return;
#if CONVOY_BLE_ENABLE
    // Runtime source: no top status pill (the waiting card carries the messaging
    // now). The BLE task's convoy_set_* data drives the radar; before a source
    // connects, convoy_self_fix is false so the tappable waiting panel shows.
    // Radio bring-up failed (BT controller out of internal RAM): say so on the
    // waiting panel instead of sitting on ACQUIRING forever. Tapping it reopens
    // the picker, which retries.
    // The radar only leaves the waiting panel once a position arrives, so a linked
    // T-Beam sitting indoors (Meshtastic reports 0,0 = no fix) looked identical to
    // "never connected". Report the link state separately from the fix state.
    static const char * wait_shown = NULL;
    const char * line = "Waiting for Mesh/Phone";
    const char * cta  = "Tap to choose source";
    // During the auto-arm stagger convoy_src_sel is still CVS_NONE, but we already
    // know what is about to come up — treat it as the effective source so the panel
    // doesn't flick back to "Waiting" for the first ~700ms of every entry.
    cvs_source_t eff = (convoy_src_sel != CVS_NONE) ? convoy_src_sel : convoy_autoarm_sel;
    if (convoy_radio_failed) {
        line = "Radio unavailable";      cta = "Tap to retry";
#if CONVOY_WIFI_SPIKE
    } else if (convoy_wifi_status() == CONVOY_WIFI_NO_NET) {
        line = "No Wi-Fi saved";         cta = "Settings > Wi-Fi to add one";
    } else if (convoy_wifi_status() == CONVOY_WIFI_UNPAIRED) {
        // Name the board so the user knows which entry to tap in the app's list.
        line = "Tap Connect in the app";  cta = convoy_wifi_device_name();
    } else if (convoy_wifi_status() == CONVOY_WIFI_WAITING) {
        line = "Linked - waiting for cars"; cta = CONVOY_PHONE_URL;
#endif
    } else if (eff == CVS_MESH && convoy_link_status() == CONVOY_LINK_ONLINE) {
        line = "Mesh linked - no GPS fix"; cta = "T-Beam needs sky view";
    } else if (eff == CVS_MESH) {
        line = "Connecting to T-Beam";   cta = "Tap to change source";
    } else if (eff == CVS_PHONE) {
        line = "Connect via browser";    cta = CONVOY_PHONE_URL;
    }
    if (line != wait_shown) {            // only touch LVGL when the message changes
        wait_shown = line;
        convoy_set_wait_text(line, cta);
    }
    convoy_set_status(NULL);
    convoy_refresh();
    return;
#else
    // When the real T-Beam link is up, its task feeds convoy_set_*; just redraw.
    if (convoy_link_status() == CONVOY_LINK_ONLINE) { convoy_refresh(); return; }
    static float phase = 0; phase += 0.02f;
    const double slat = 12.9716, slon = 77.5946;          // mock ~Bengaluru
    convoy_set_self(slat, slon, true);
    double hdg = 300.0 + phase * 6.0 + sinf(phase * 0.5f) * 35.0;   // slow weave
    convoy_set_heading(hdg, true);
    const double mlat = 111320.0, mlon = 111320.0 * cos(cv_d2r(slat));
    struct { const char * n; float b0; float d; float s; uint32_t c; } defs[] = {
        { "C2",  40,  650,  1.0f, 0x00E5FF }, { "C3", 155, 1250, -0.7f, 0x00E676 },
        { "C4", 250,  320,  1.6f, 0xFFD54F }, { "C5", 310, 1900,  0.4f, 0xFF4081 },
    };
    for (int i = 0; i < 4; i++) {
        float brg  = defs[i].b0 + phase * defs[i].s * 20.0f;
        float dist = defs[i].d  + sinf(phase * 0.6f + i) * 120.0f;
        double dN = dist * cos(cv_d2r(brg)), dE = dist * sin(cv_d2r(brg));
        convoy_set_car(i, defs[i].n, slat + dN / mlat, slon + dE / mlon,
                       lv_color_hex(defs[i].c), true, true);
    }
    convoy_refresh();
#endif
}

extern "C" void convoy_open_screen() {
    convoy_build_screen();
    convoy_src_build_screen();
    // Fresh entry: clear any loader state left behind by a previous visit that was
    // exited mid-transition, else the busy flag would swallow every tap in here.
    convoy_load_busy = false; convoy_pending_scr = NULL;
    convoy_ready_scr = NULL;  convoy_apply_sel   = false;
    convoy_settings_cb        = fw_open_picker;   // radar panel tap → source picker
    convoy_src_on_scan        = fw_src_scan;
    convoy_src_on_pick_device = fw_src_pick;
    convoy_src_on_use_phone   = fw_src_phone;
    convoy_src_on_back        = fw_src_back;
    convoy_src_load_fn        = convoy_load_clean;   // clean AMOLED loads for the picker
    static lv_timer_t * ctmr = nullptr;
    if (!ctmr) ctmr = lv_timer_create(convoy_mock_tick, 200, NULL);
    static lv_timer_t * ptmr = nullptr;
    if (!ptmr) ptmr = lv_timer_create(convoy_picker_tick, 250, NULL);
    currentMode = MODE_UI;                 // treat as a normal UI screen

    // Restore the last-used source; first-ever entry opens the picker.
    Preferences p; p.begin("convoy", true);
    int saved = p.getInt("source", 0);
    String mac = p.getString("tbeam_mac", "");
    p.end();
    // AUTO-ARM ON ENTRY: apply the remembered source and bring the radio up straight
    // away, so coming back from another screen reconnects without a tap.
    //
    // This was deliberately display-only before, because auto-arming rebooted the
    // board (NimBLE init at ~21KB free → controller malloc fails → assert). Both
    // causes are now fixed: NimBLEDevice::init()'s return is checked
    // (convoy_ble_guard.h), and the WiFi handover waits for a real ack from the OBD
    // worker instead of powering the radio down from under it. A bring-up failure
    // now surfaces as "Radio unavailable / Tap to retry" instead of panicking.
    //
    // The radio can NOT simply stay up while you are elsewhere: BLE and WiFi cannot
    // coexist on this build and the speedometer needs WiFi for OBD. So re-entry
    // *reconnects* (~2-5s, dominated by the WiFi release) rather than resuming a
    // still-live link. The source choice itself persists in NVS across reboots.
    // STAGGERED: the screen load below happens first; convoy_autoarm_schedule() brings
    // the radio up ~700ms later, once the transition has settled. We must NOT call
    // convoy_load_when_ready() here either — the screen load below already puts the
    // radar up, and two loaders would fight.
    convoy_src_sel      = CVS_NONE;
    convoy_role_ready   = false;
    convoy_radio_failed = false;
    if (saved == 2) {
        convoy_set_wait_text("Connect via browser", CONVOY_PHONE_URL);
        convoy_autoarm_schedule(CVS_PHONE);
    } else if (saved == 1 && mac.length() >= 17) {
        strncpy(convoy_sel_mac, mac.c_str(), sizeof(convoy_sel_mac) - 1);
        convoy_sel_mac[sizeof(convoy_sel_mac) - 1] = 0;
        convoy_set_wait_text("Connecting to T-Beam", "Tap to change source");
        convoy_autoarm_schedule(CVS_MESH);
    } else {
        convoy_set_wait_text("No source selected", "Tap to choose");
    }
    convoy_set_self(0, 0, false);          // start on the waiting panel

    // First-ever entry opens the picker; with a remembered source show the radar
    // (its waiting panel taps through to the picker to connect).
    if (saved == 1 || saved == 2)
        lv_scr_load_anim(convoy_screen, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, false);
    else
        convoy_load_clean(convoy_src_screen);
}
