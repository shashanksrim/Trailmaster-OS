#include "ui.h"
#include "ui_gridlauncher.h"

lv_obj_t * ui_uigridlauncher = NULL;

extern const lv_img_dsc_t ui_img_icon_gauges;
extern const lv_img_dsc_t ui_img_icon_speed;
extern const lv_img_dsc_t ui_img_icon_incline;
extern const lv_img_dsc_t ui_img_icon_image;
extern const lv_img_dsc_t ui_img_icon_game;
extern const lv_img_dsc_t ui_img_icon_settings;
extern const lv_img_dsc_t ui_img_icon_about;

// Event handlers
static void ui_event_grid_btn(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED) {
        int id = (int)(intptr_t)lv_event_get_user_data(e);
        switch(id) {
            case 0: _ui_screen_change(&ui_uigauge, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_uigauge_screen_init); break;
            case 1: { extern void ui_event_Buttonspeedometer(lv_event_t*); ui_event_Buttonspeedometer(e); } break;
            case 2: { extern void ui_event_Buttoninclinometer(lv_event_t*); ui_event_Buttoninclinometer(e); } break;
            case 3: { extern void ui_event_Buttongauges(lv_event_t*); ui_event_Buttongauges(e); } break;
            case 4: { extern void ui_event_Buttonimageframe(lv_event_t*); ui_event_Buttonimageframe(e); } break;
            case 5: { extern void ui_event_Buttongame(lv_event_t*); ui_event_Buttongame(e); } break;
            case 6: { extern void ui_event_Buttonsettings(lv_event_t*); ui_event_Buttonsettings(e); } break;
            case 7: { extern void ui_event_Buttonabout(lv_event_t*); ui_event_Buttonabout(e); } break;
        }
    }
}

static void ui_event_close_btn(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        extern void exit_launcher();
        exit_launcher();
    }
}

static void ui_event_grid_gesture(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_TOP) {
            extern void exit_launcher();
            exit_launcher();
        }
    }
}

// Helper to create an app icon button
static lv_obj_t * create_app_icon(lv_obj_t * parent, const char * label_text, const lv_img_dsc_t * img_src, lv_color_t accent_color, int id) {
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_width(btn, 120);
    lv_obj_set_height(btn, 120);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1c2620), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x253228), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, accent_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, accent_color, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(btn, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // Icon image
    lv_obj_t * img = lv_img_create(btn);
    lv_img_set_src(img, img_src);
    lv_obj_set_style_img_recolor_opa(img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor(img, accent_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -10);
    
    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x5a7060), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    lv_obj_add_event_cb(btn, ui_event_grid_btn, LV_EVENT_CLICKED, (void*)(intptr_t)id);
    return btn;
}

lv_obj_t * ui_gridlauncher_container;

void ui_uigridlauncher_screen_init(void) {
    ui_uigridlauncher = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_uigridlauncher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_uigridlauncher, lv_color_hex(0x0d0f0e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_uigridlauncher, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Top bar "Trailmaster" text
    lv_obj_t * top_lbl = lv_label_create(ui_uigridlauncher);
    lv_label_set_text(top_lbl, "Trailmaster");
    lv_obj_set_style_text_color(top_lbl, lv_color_hex(0x5a7060), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(top_lbl, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(top_lbl, LV_ALIGN_TOP_MID, 0, 40);

    // Grid container
    ui_gridlauncher_container = lv_obj_create(ui_uigridlauncher);
    lv_obj_t * grid = ui_gridlauncher_container;
    lv_obj_set_size(grid, 400, 320);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(grid, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(grid, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(grid, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(grid, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Add apps
    extern const lv_img_dsc_t ui_img_icon_speed;
    extern const lv_img_dsc_t ui_img_icon_incline;
    extern const lv_img_dsc_t ui_img_icon_gauges;
    extern const lv_img_dsc_t ui_img_icon_image;
    extern const lv_img_dsc_t ui_img_icon_game;
    create_app_icon(grid, "SPEED", &ui_img_icon_speed, lv_color_hex(0xef4444), 1);
    create_app_icon(grid, "INCLINE", &ui_img_icon_incline, lv_color_hex(0xeab308), 2);
    create_app_icon(grid, "GAUGES", &ui_img_icon_gauges, lv_color_hex(0x4ade80), 3);
    create_app_icon(grid, "IMAGE", &ui_img_icon_image, lv_color_hex(0x3b82f6), 4);
    create_app_icon(grid, "GAMES", &ui_img_icon_game, lv_color_hex(0xf97316), 5);
    create_app_icon(grid, "SETTINGS", &ui_img_icon_settings, lv_color_hex(0x5a7060), 6);
    create_app_icon(grid, "ABOUT", &ui_img_icon_about, lv_color_hex(0xf87171), 7);

    // Close button (bottom pill)
    lv_obj_t * close_btn = lv_btn_create(ui_uigridlauncher);
    lv_obj_set_size(close_btn, 80, 8);
    lv_obj_set_style_radius(close_btn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2a3d33), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x5a7060), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_shadow_width(close_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // Add gradient overlay to simulate fade at the bottom over the grid (30px total height)
    lv_obj_t * grad1 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad1, 466, 6);
    lv_obj_align(grad1, LV_ALIGN_TOP_MID, 0, 390);
    lv_obj_set_style_bg_opa(grad1, 51, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad1, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad1, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad1, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad2 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad2, 466, 6);
    lv_obj_align(grad2, LV_ALIGN_TOP_MID, 0, 396);
    lv_obj_set_style_bg_opa(grad2, 102, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad2, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad2, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad2, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad3 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad3, 466, 6);
    lv_obj_align(grad3, LV_ALIGN_TOP_MID, 0, 402);
    lv_obj_set_style_bg_opa(grad3, 153, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad3, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad3, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad3, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad4 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad4, 466, 6);
    lv_obj_align(grad4, LV_ALIGN_TOP_MID, 0, 408);
    lv_obj_set_style_bg_opa(grad4, 204, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad4, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad4, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad4, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad5 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad5, 466, 6);
    lv_obj_align(grad5, LV_ALIGN_TOP_MID, 0, 414);
    lv_obj_set_style_bg_opa(grad5, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad5, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad5, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad5, LV_OBJ_FLAG_CLICKABLE);
    
    // Add event to close btn (simulates a swipe up to exit_launcher)
    lv_obj_add_event_cb(close_btn, ui_event_close_btn, LV_EVENT_CLICKED, NULL);
    
    // Add TOP gradient overlay to simulate fade at the top over the grid (30px total height)
    lv_obj_t * grad_top1 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad_top1, 466, 6);
    lv_obj_align(grad_top1, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_opa(grad_top1, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad_top1, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad_top1, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad_top1, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad_top2 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad_top2, 466, 6);
    lv_obj_align(grad_top2, LV_ALIGN_TOP_MID, 0, 91);
    lv_obj_set_style_bg_opa(grad_top2, 204, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad_top2, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad_top2, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad_top2, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad_top3 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad_top3, 466, 6);
    lv_obj_align(grad_top3, LV_ALIGN_TOP_MID, 0, 97);
    lv_obj_set_style_bg_opa(grad_top3, 153, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad_top3, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad_top3, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad_top3, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad_top4 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad_top4, 466, 6);
    lv_obj_align(grad_top4, LV_ALIGN_TOP_MID, 0, 103);
    lv_obj_set_style_bg_opa(grad_top4, 102, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad_top4, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad_top4, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad_top4, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * grad_top5 = lv_obj_create(ui_uigridlauncher);
    lv_obj_set_size(grad_top5, 466, 6);
    lv_obj_align(grad_top5, LV_ALIGN_TOP_MID, 0, 109);
    lv_obj_set_style_bg_opa(grad_top5, 51, LV_PART_MAIN);
    lv_obj_set_style_bg_color(grad_top5, lv_color_hex(0x0d0f0e), LV_PART_MAIN);
    lv_obj_set_style_border_width(grad_top5, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grad_top5, LV_OBJ_FLAG_CLICKABLE);

    // Add BOTTOM gradient overlay to simulate fade at the bottom over the grid (30px total height)
    lv_obj_add_event_cb(ui_uigridlauncher, ui_event_grid_gesture, LV_EVENT_GESTURE, NULL);
}
