import sys

# 1. Modify PhotoFrameApp.h
with open('PhotoFrameApp.h', 'r') as f:
    header = f.read()
if 'void pf_show_upload_overlay(void);' not in header:
    header = header.replace('void switch_to_launcher();', 'void switch_to_launcher();\nvoid pf_show_upload_overlay(void);')
    with open('PhotoFrameApp.h', 'w') as f:
        f.write(header)


# 2. Modify PhotoFrameApp.cpp
with open('PhotoFrameApp.cpp', 'r') as f:
    cpp = f.read()

cpp = cpp.replace('static void pf_show_upload_overlay(void) {', 'void pf_show_upload_overlay(void) {')
cpp = cpp.replace('if (!photoframe_screen || upload_overlay_open) return;', 'if (upload_overlay_open) return;')
cpp = cpp.replace('amoled.fillScreen(AMOLED_COLOR_BLACK);', '// amoled.fillScreen(AMOLED_COLOR_BLACK);')
cpp = cpp.replace('pf_upload_overlay = lv_obj_create(photoframe_screen);', 'pf_upload_overlay = lv_obj_create(lv_scr_act());')

with open('PhotoFrameApp.cpp', 'w') as f:
    f.write(cpp)


# 3. Modify INO
with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'r') as f:
    ino = f.read()

# Replace Page 2 UI
page2_old = """    extern const lv_img_dsc_t ui_img_trailmaster;
    lv_obj_t * about_logo = lv_img_create(page2);
    lv_img_set_src(about_logo, &ui_img_trailmaster);
    lv_img_set_zoom(about_logo, 160); // smaller logo (was 276)

    lv_obj_t * ver_lbl = lv_label_create(page2);
    lv_label_set_text(ver_lbl, "v3.0");
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_14, 0); // smaller font
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x888888), 0);

    lv_obj_t * scr_rows = page2;

    // ─── ROW: Software Update ─────────────────────────────────────────────────
    // Section header
    lv_obj_t * lbl_update_hdr = lv_label_create(scr_rows);
    lv_label_set_text(lbl_update_hdr, "SOFTWARE UPDATE");
    lv_obj_set_style_text_font(lbl_update_hdr, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_update_hdr, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_width(lbl_update_hdr, 430);

    // Version row
    lv_obj_t * row_ver = lv_obj_create(scr_rows);
    lv_obj_set_size(row_ver, 430, 60);
    lv_obj_set_style_bg_color(row_ver, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_ver, 255, 0);
    lv_obj_set_style_border_width(row_ver, 1, 0);
    lv_obj_set_style_border_color(row_ver, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_ver, 12, 0);
    lv_obj_clear_flag(row_ver, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * lbl_ver_key = lv_label_create(row_ver);
    lv_label_set_text(lbl_ver_key, "Current version");
    lv_obj_set_style_text_font(lbl_ver_key, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_ver_key, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(lbl_ver_key, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t * lbl_ver_val = lv_label_create(row_ver);
    lv_label_set_text_fmt(lbl_ver_val, "v%s", ota_current_version());
    lv_obj_set_style_text_font(lbl_ver_val, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_ver_val, lv_color_hex(0xFF9500), 0);
    lv_obj_align(lbl_ver_val, LV_ALIGN_RIGHT_MID, -14, 0);

    // Status row
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
    lv_label_set_text(lbl_ota_status, ota_get_status()->status_text);
    lv_obj_set_style_text_font(lbl_ota_status, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_ota_status, LV_ALIGN_LEFT_MID, 14, 0);
    lv_label_set_long_mode(lbl_ota_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_ota_status, 400);

    // WiFi networks row
    lv_obj_t * row_wifi = lv_obj_create(scr_rows);
    lv_obj_set_size(row_wifi, 430, 64);
    lv_obj_set_style_bg_color(row_wifi, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_wifi, 255, 0);
    lv_obj_set_style_border_width(row_wifi, 1, 0);
    lv_obj_set_style_border_color(row_wifi, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_wifi, 12, 0);
    lv_obj_clear_flag(row_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * lbl_wifi_hint = lv_label_create(row_wifi);
    lv_label_set_text(lbl_wifi_hint, LV_SYMBOL_WIFI "  Add networks via 192.168.4.1");
    lv_obj_set_style_text_font(lbl_wifi_hint, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(lbl_wifi_hint, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_wifi_hint, LV_ALIGN_LEFT_MID, 14, 0);"""

page2_new = """    extern const lv_img_dsc_t ui_img_trailmaster;
    lv_obj_t * about_logo = lv_img_create(page2);
    lv_img_set_src(about_logo, &ui_img_trailmaster);
    lv_img_set_zoom(about_logo, 160); // smaller logo (was 276)
    // Reduce padding below logo to 5px gap before version text
    lv_obj_set_style_pad_bottom(about_logo, 0, 0);

    lv_obj_t * ver_lbl = lv_label_create(page2);
    lv_label_set_text_fmt(ver_lbl, "v%s", ota_current_version());
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_14, 0); // smaller font
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_pad_top(ver_lbl, -5, 0); // Force closer to logo

    lv_obj_t * scr_rows = page2;

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
    }, LV_EVENT_ALL, NULL);"""

if page2_old in ino:
    ino = ino.replace(page2_old, page2_new)
    with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'w') as f:
        f.write(ino)
    print("INO updated successfully")
else:
    print("Could not find the target text in INO!")
    sys.exit(1)
