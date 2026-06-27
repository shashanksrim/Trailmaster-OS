import sys

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'r') as f:
    lines = f.readlines()

# Extract OTA block (lines 630 to 748 are indices 629 to 747)
ota_block = lines[629:748]

# Remove OTA block
del lines[629:748]

text = "".join(lines)

# 1. Change NUM_PAGES = 2 to 3
text = text.replace("const int NUM_PAGES = 2;", "const int NUM_PAGES = 3;")

# 2. Modify AboutPageData struct
struct_old = """    struct AboutPageData {
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

struct_new = """    struct AboutPageData {
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

text = text.replace(struct_old, struct_new)

# 3. Create page3 and insert OTA block before AboutPageData
page3_init = """
    // ══════════════════════════════════════════════════════════════════════
    // PAGE 3 — Software Update
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t * page3 = lv_obj_create(pages);
    lv_obj_set_size(page3, 466, 360);
    lv_obj_set_style_bg_opa(page3, 0, 0);
    lv_obj_set_style_border_width(page3, 0, 0);
    lv_obj_set_style_pad_all(page3, 0, 0);
    lv_obj_clear_flag(page3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(page3, 10, 0);
    lv_obj_set_style_pad_gap(page3, 10, 0);

    lv_obj_t * scr_rows = page3;

"""

ota_str = "".join(ota_block)

insert_target = "    // ── Dot update on scroll ───────────────────────────────────────────────"
insert_payload = page3_init + ota_str + "\n" + insert_target

text = text.replace(insert_target, insert_payload)

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'w') as f:
    f.write(text)

print("Done")
