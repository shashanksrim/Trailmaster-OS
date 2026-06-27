import sys

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'r') as f:
    code = f.read()

old_block = """    lv_obj_t * scr_rows = page2;

    // Status row (Empty box) - We add a placeholder text so it's not totally empty if status is idle
    lv_obj_t * row_status = lv_obj_create(scr_rows);
    lv_obj_set_size(row_status, 430, 60);
    lv_obj_set_style_bg_color(row_status, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(row_status, 255, 0);
    lv_obj_set_style_border_width(row_status, 1, 0);
    lv_obj_set_style_border_color(row_status, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(row_status, 12, 0);
    lv_obj_clear_flag(row_status, LV_OBJ_FLAG_SCROLLABLE);
    static lv_obj_t * lbl_ota_status = NULL;
    lbl_ota_status = lv_label_create(row_status);
    const char * current_status = ota_get_status()->status_text;
    lv_label_set_text(lbl_ota_status, strlen(current_status) > 0 ? current_status : "Ready for update check...");
    lv_obj_set_style_text_font(lbl_ota_status, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_ota_status, LV_ALIGN_LEFT_MID, 14, 0);
    lv_label_set_long_mode(lbl_ota_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_ota_status, 400);

    // WiFi networks button (QR code launcher)
    lv_obj_t * btn_wifi = lv_btn_create(scr_rows);
    lv_obj_set_size(btn_wifi, 430, 64);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(btn_wifi, 1, 0);
    lv_obj_set_style_border_color(btn_wifi, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_wifi, 12, 0);
    lv_obj_t * lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  Connect via QR / Wi-Fi");
    lv_obj_set_style_text_font(lbl_wifi, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 14, 0);
    
    extern void pf_show_upload_overlay(void);
    lv_obj_add_event_cb(btn_wifi, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            pf_show_upload_overlay();
        }
    }, LV_EVENT_ALL, NULL);

    // Check for Update / Install button
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
    lv_obj_center(lbl_btn_check);"""

new_block = """    lv_obj_t * scr_rows = page2;

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

    // 2. Status row
    lv_obj_t * row_status = lv_obj_create(scr_rows);
    lv_obj_set_size(row_status, 430, 60);
    lv_obj_set_style_bg_color(row_status, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(row_status, 255, 0);
    lv_obj_set_style_border_width(row_status, 1, 0);
    lv_obj_set_style_border_color(row_status, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(row_status, 12, 0);
    lv_obj_clear_flag(row_status, LV_OBJ_FLAG_SCROLLABLE);
    static lv_obj_t * lbl_ota_status = NULL;
    lbl_ota_status = lv_label_create(row_status);
    const char * current_status = ota_get_status()->status_text;
    lv_label_set_text(lbl_ota_status, strlen(current_status) > 0 ? current_status : "Ready for update check...");
    lv_obj_set_style_text_font(lbl_ota_status, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_ota_status, LV_ALIGN_LEFT_MID, 14, 0);
    lv_label_set_long_mode(lbl_ota_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_ota_status, 400);

    // 3. Settings icon button (QR code launcher)
    lv_obj_t * btn_wifi = lv_btn_create(scr_rows);
    lv_obj_set_size(btn_wifi, 64, 64);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(btn_wifi, 1, 0);
    lv_obj_set_style_border_color(btn_wifi, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_wifi, 32, 0); // circle
    lv_obj_t * lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_wifi);
    
    extern void pf_show_upload_overlay(void);
    lv_obj_add_event_cb(btn_wifi, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            pf_show_upload_overlay();
        }
    }, LV_EVENT_ALL, NULL);"""

if old_block in code:
    code = code.replace(old_block, new_block)
else:
    print("Could not find about UI block in ino file")

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'w') as f:
    f.write(code)

with open('PhotoFrameApp.cpp', 'r') as f:
    code = f.read()

code = code.replace("<button class='primary' type='submit'>Save Network</button>", "<button class='primary' type='submit'>Sync to Device</button>")

with open('PhotoFrameApp.cpp', 'w') as f:
    f.write(code)

with open('OTAManager.cpp', 'r') as f:
    code = f.read()

code = code.replace('set_status(OTA_SCANNING_WIFI, 0, "Scanning for known networks...");', 'set_status(OTA_SCANNING_WIFI, 0, "Scanning for WiFi networks...");')
code = code.replace('set_status(OTA_FAILED_NO_WIFI, 0, "No WiFi networks found");', 'set_status(OTA_FAILED_NO_WIFI, 0, "Failed: No WiFi network found");')
code = code.replace('set_status(OTA_FAILED_NO_WIFI, 0, "No saved networks in range. Add via 192.168.4.1");', 'set_status(OTA_FAILED_NO_WIFI, 0, "Failed: No WiFi network found (Add via QR settings)");')
code = code.replace('snprintf(buf, sizeof(buf), "Connected to %s", saved_ssid.c_str());', 'snprintf(buf, sizeof(buf), "WiFi connected, checking GitHub...");')

old_github_fail1 = """    if (code != 200) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HTTP check failed (%d)", code);
        set_status(OTA_FAILED_SERVER, 0, buf);"""
new_github_fail1 = """    if (code != 200) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Failed: Github not found (%d)", code);
        set_status(OTA_FAILED_SERVER, 0, buf);"""
code = code.replace(old_github_fail1, new_github_fail1)

with open('OTAManager.cpp', 'w') as f:
    f.write(code)

print("Updates applied")
