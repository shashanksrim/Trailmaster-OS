#ifndef PHOTO_FRAME_APP_H
#define PHOTO_FRAME_APP_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
#include "lvgl.h"

// --- Photo Frame Globals ---
extern WebServer photo_server;
extern DNSServer photo_dnsServer;
extern std::vector<String> image_files;
extern int current_image_index;
extern volatile bool new_image_uploaded;
extern unsigned long last_image_change;
extern const unsigned long SLIDESHOW_INTERVAL;
extern bool delete_dialog_open;

extern lv_obj_t * photoframe_screen;
extern lv_obj_t * img_obj;
extern lv_obj_t * wifi_screen_cont;
extern uint8_t * psram_buffer;
extern lv_img_dsc_t img_dsc;

// --- Function Prototypes ---
void build_photoframe_screen();
void switch_to_photoframe();
void scan_images();
void show_image(int index);
void next_image();
void prev_image();
void delete_current_image();
void photoframe_setup();
void photoframe_loop_handler();

// WiFi Management
void start_photoframe_wifi();
void stop_photoframe_wifi();
lv_obj_t * show_boot_splash();

// Custom Boot Time Setting
int get_custom_boot_time();
void set_custom_boot_time(int val);

// Launcher Bridge
#ifdef __cplusplus
extern "C" {
void pf_show_wifi_menu(void);   // Wi-Fi settings: saved networks | configure

#endif
void switch_to_launcher();
// wifi_only: Wi-Fi setup (settings, first run) — no "Enable Wi-Fi" toggle, the
// hotspot starts by itself, large QR. false: the photo-upload gateway, which
// keeps the toggle. No default argument — this is declared extern "C", and every
// caller should have to state which screen it wants.
void pf_show_upload_overlay(bool wifi_only);
void force_full_ui_redraw(lv_obj_t * target_scr);
#ifdef __cplusplus
}
#endif

// C++ linkage on purpose: only the .ino (C++) calls it.
void pf_show_wifi_menu(void);   // Wi-Fi settings: saved networks | configure

#endif // PHOTO_FRAME_APP_H