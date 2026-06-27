#ifndef PHOTO_FRAME_APP_H
#define PHOTO_FRAME_APP_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
#include "lvgl.h"

// --- Photo Frame Globals ---
extern const char* AP_SSID;
extern const char* AP_PASSWORD;
extern WebServer server;
extern DNSServer dnsServer;
extern const byte DNS_PORT;
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

#endif // PHOTO_FRAME_APP_H
