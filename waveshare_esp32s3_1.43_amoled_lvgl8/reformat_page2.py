import sys

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'r') as f:
    text = f.read()

# 1. Change NUM_PAGES back to 2
text = text.replace("const int NUM_PAGES = 3;", "const int NUM_PAGES = 2;")

# 2. Extract OTA block from page3
start_str = "    // ─── ROW: Software Update ─────────────────────────────────────────────────"
end_str = "    // ── Dot update on scroll ───────────────────────────────────────────────"

start_idx = text.find(start_str)
end_idx = text.find(end_str)

if start_idx == -1 or end_idx == -1:
    print("Could not find OTA block bounds")
    sys.exit(1)

ota_block = text[start_idx:end_idx]

# 3. Build new Page 2 replacement
page2_old_start = "    // ══════════════════════════════════════════════════════════════════════\n    // PAGE 2"
page2_start_idx = text.find(page2_old_start)

if page2_start_idx == -1:
    print("Could not find Page 2")
    sys.exit(1)

page2_new = """    // ══════════════════════════════════════════════════════════════════════
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

    lv_obj_t * ver_lbl = lv_label_create(page2);
    lv_label_set_text(ver_lbl, "v3.0");
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_14, 0); // smaller font
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x888888), 0);

    lv_obj_t * scr_rows = page2;

"""

# 4. Remove everything from Page 2 start to End of Page 3 (just before dot update)
text_before_page2 = text[:page2_start_idx]
text_after_page3 = text[end_idx:]

text = text_before_page2 + page2_new + ota_block + "\n" + text_after_page3

# 5. Fix AboutPageData struct
struct_old = """    struct AboutPageData {
        lv_obj_t * pages;
        lv_obj_t * dot0;
        lv_obj_t * dot1;
        lv_obj_t * dot2;
    };
    AboutPageData * pd = new AboutPageData { pages, dots[0], dots[1], dots[2] };

    lv_obj_add_event_cb(pages, [](lv_event_t * ev) {
        AboutPageData * d = (AboutPageData *)lv_event_get_user_data(ev);
        lv_coord_t scroll_x = lv_obj_get_scroll_x(d->pages);
        int page_idx = (scroll_x + 233) / 466; // snap to nearest page
        lv_obj_set_style_bg_color(d->dot0, lv_color_hex(page_idx == 0 ? 0xFF9800 : 0x444444), 0);
        lv_obj_set_style_bg_color(d->dot1, lv_color_hex(page_idx == 1 ? 0xFF9800 : 0x444444), 0);
        lv_obj_set_style_bg_color(d->dot2, lv_color_hex(page_idx == 2 ? 0xFF9800 : 0x444444), 0);
    }, LV_EVENT_SCROLL, pd);"""

struct_new = """    struct AboutPageData {
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
    }, LV_EVENT_SCROLL, pd);"""

text = text.replace(struct_old, struct_new)

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'w') as f:
    f.write(text)

print("Done formatting Page 2")
