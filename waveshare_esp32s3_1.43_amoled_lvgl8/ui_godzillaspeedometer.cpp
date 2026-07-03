#include "ui.h"
#include "ui_godzillaspeedometer.h"
#include <math.h>
#include <stdio.h>
#include <AnimatedGIF.h>
#include "esp_heap_caps.h"

#include <Preferences.h>
static Preferences prefs;

extern volatile int car_rpm;
extern volatile int car_speed;

bool is_godzilla_animating = false;
lv_obj_t *ui_godzillaspeedometer = NULL;
lv_obj_t *ui_godzilla_canvas = NULL;
lv_obj_t *ui_godzilla_speed_label = NULL;
lv_obj_t * ui_godzilla_speed_label_shadows[8] = {NULL};
lv_obj_t * ui_godzilla_unit_label = NULL;
lv_obj_t * ui_godzilla_unit_label_shadows[8] = {NULL};
lv_obj_t *ui_godzilla_rpm_arc = NULL;
lv_obj_t *godzilla_tick_lines[41] = {NULL};
lv_obj_t *godzilla_tick_labels[9] = {NULL};
lv_obj_t *ui_godzilla_x1000_label = NULL;

int default_speedometer = 0;
bool is_simulating_obd = false;

void load_speedo_preferences() {
    prefs.begin("hellojimny", false);
    default_speedometer = prefs.getInt("def_speedo", 0);
    prefs.end();
    Serial.printf("[SETTINGS] Loaded default_speedometer = %d\n", default_speedometer);
}

void save_speedo_preferences(int val) {
    default_speedometer = val;
    prefs.begin("hellojimny", false);
    prefs.putInt("def_speedo", val);
    prefs.end();
    Serial.printf("[SETTINGS] Saved default_speedometer = %d\n", default_speedometer);
}

// Shared with the simulator (sim/) — see godzilla_speedo_ui.h. Editing that
// file changes what BOTH the device and the sim render; there's only one copy.
#include "godzilla_speedo_ui.h"

// --- AnimatedGIF Implementation ---
static AnimatedGIF gif;
static lv_color_t * canvas_buf = NULL;
static lv_timer_t * gif_timer = NULL;

// Single active GIF buffer - only ONE loaded at a time to stay within PSRAM limits
// We are reverting to pre-loading all GIFs to PSRAM for instant switching.
static uint8_t * gif_idle = NULL;
static uint8_t * gif_medium = NULL;
static uint8_t * gif_max = NULL;
static uint8_t * gif_redline = NULL;
static uint8_t * gif_still = NULL;
static size_t size_idle = 0, size_medium = 0, size_max = 0, size_redline = 0, size_still = 0;

static gif_state_t current_gif_state = GIF_STATE_NONE;
static gif_state_t requested_gif_state = GIF_STATE_IDLE;

static const char * GIF_PATH_IDLE   = "/sd_card/speedometergif/rpm_idle.gif";
static const char * GIF_PATH_MEDIUM = "/sd_card/speedometergif/rpm_medium.gif";
static const char * GIF_PATH_MAX    = "/sd_card/speedometergif/rpm_max.gif";
static const char * GIF_PATH_REDLINE = "/sd_card/speedometergif/rpm_redline.gif";

#define CANVAS_WIDTH  272
#define CANVAS_HEIGHT 272

#define JIMNJAP_WIDTH 187
#define JIMNJAP_HEIGHT 55
static lv_color_t * jimnjap_canvas_buf = NULL;
static lv_obj_t * ui_jimnjap_canvas = NULL;
static lv_obj_t * ui_trailmaster_jap_img = NULL;
static lv_obj_t * ui_jimnjap_label = NULL;
static uint8_t * jimnjap_gif_data = NULL;
static size_t size_jimnjap = 0;

static void JimnjapGIFDraw(GIFDRAW *pDraw) {
    if (!jimnjap_canvas_buf) return;
    int x, y, iWidth;
    iWidth = pDraw->iWidth;
    if (pDraw->iX + iWidth > JIMNJAP_WIDTH) iWidth = JIMNJAP_WIDTH - pDraw->iX;
    if (iWidth <= 0) return;
    y = pDraw->iY + pDraw->y;
    if (y >= JIMNJAP_HEIGHT || pDraw->iX >= JIMNJAP_WIDTH) return;
    uint8_t *s = pDraw->pPixels;
    lv_color_t * line_ptr = jimnjap_canvas_buf + (y * JIMNJAP_WIDTH) + pDraw->iX;
    uint16_t *usPalette = pDraw->pPalette;
    if (pDraw->ucHasTransparency) {
        uint8_t ucT = pDraw->ucTransparent;
        for (x = 0; x < iWidth; x++) {
            uint8_t c = *s++;
            if (c != ucT) line_ptr[x].full = usPalette[c];
        }
    } else {
        for (x = 0; x < iWidth; x++) line_ptr[x].full = usPalette[*s++];
    }
}

// GIF Draw Callback - renders one GIF scanline into the LVGL canvas buffer
static void GIFDraw(GIFDRAW *pDraw) {
    if (!canvas_buf) return;
    int x, y, iWidth;
    iWidth = pDraw->iWidth;
    if (pDraw->iX + iWidth > CANVAS_WIDTH) iWidth = CANVAS_WIDTH - pDraw->iX;
    if (iWidth <= 0) return;
    y = pDraw->iY + pDraw->y;
    if (y >= CANVAS_HEIGHT || pDraw->iX >= CANVAS_WIDTH) return;
    uint8_t *s = pDraw->pPixels;
    lv_color_t * line_ptr = canvas_buf + (y * CANVAS_WIDTH) + pDraw->iX;
    uint16_t *usPalette = pDraw->pPalette;
    if (pDraw->ucHasTransparency) {
        uint8_t ucT = pDraw->ucTransparent;
        for (x = 0; x < iWidth; x++) {
            uint8_t c = *s++;
            if (c != ucT) line_ptr[x].full = usPalette[c];
        }
    } else {
        for (x = 0; x < iWidth; x++) line_ptr[x].full = usPalette[*s++];
    }
}

static const char * get_gif_path(gif_state_t state) {
    if (state == GIF_STATE_IDLE)       return GIF_PATH_IDLE;
    if (state == GIF_STATE_INCREASING) return GIF_PATH_MEDIUM;
    if (state == GIF_STATE_MAX)        return GIF_PATH_MAX;
    if (state == GIF_STATE_REDLINE)    return GIF_PATH_REDLINE;
    return NULL;
}

static uint8_t * load_gif_to_psram(const char * path, size_t * out_size) {
    FILE * f = fopen(path, "r");
    if (!f) {
        Serial.printf("[GIF] MISSING: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t * buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.printf("[GIF] OOM for %s (%d bytes)\n", path, (int)size);
        fclose(f);
        return NULL;
    }
    fread(buf, 1, size, f);
    fclose(f);
    *out_size = size;
    Serial.printf("[GIF] Pre-loaded %s (%d bytes)\n", path, (int)size);
    return buf;
}

static void play_gif_timer_cb(lv_timer_t * t) {
    if (!ui_godzilla_canvas || !canvas_buf) return;

    if (current_gif_state != requested_gif_state) {
        if (current_gif_state == GIF_STATE_NONE) {
            // Transition from Initializing to the jimnjap image
            if (ui_jimnjap_canvas && jimnjap_canvas_buf) {
                extern int use_jimny_logo;
                if (!use_jimny_logo) {
                    if (ui_jimnjap_label) lv_obj_add_flag(ui_jimnjap_label, LV_OBJ_FLAG_HIDDEN);
                    if (ui_jimnjap_canvas) lv_obj_add_flag(ui_jimnjap_canvas, LV_OBJ_FLAG_HIDDEN);
                    if (ui_trailmaster_jap_img) lv_obj_clear_flag(ui_trailmaster_jap_img, LV_OBJ_FLAG_HIDDEN);
                } else {
                    if (!jimnjap_gif_data) jimnjap_gif_data = load_gif_to_psram("/sd_card/jimnjap.gif", &size_jimnjap);
                    if (!jimnjap_gif_data) jimnjap_gif_data = load_gif_to_psram("/sd_card/speedometergif/jimnjap.gif", &size_jimnjap);
                    if (!jimnjap_gif_data) jimnjap_gif_data = load_gif_to_psram("/sd_card/jimnjap.GIF", &size_jimnjap);
                    if (!jimnjap_gif_data) jimnjap_gif_data = load_gif_to_psram("/sd_card/speedometergif/jimnjap.GIF", &size_jimnjap);
                    if (jimnjap_gif_data && size_jimnjap > 0) {
                        if (ui_jimnjap_label) lv_obj_add_flag(ui_jimnjap_label, LV_OBJ_FLAG_HIDDEN); // hide only if we have data
                        lv_obj_clear_flag(ui_jimnjap_canvas, LV_OBJ_FLAG_HIDDEN);
                        if (ui_trailmaster_jap_img) lv_obj_add_flag(ui_trailmaster_jap_img, LV_OBJ_FLAG_HIDDEN);
                        gif.begin(LITTLE_ENDIAN_PIXELS);
                        if (gif.open(jimnjap_gif_data, (int)size_jimnjap, JimnjapGIFDraw)) {
                            int delay = 0;
                            gif.playFrame(false, &delay);
                            lv_obj_invalidate(ui_jimnjap_canvas);
                            gif.close();
                        } else {
                            Serial.println("[GIF] Failed to open jimnjap.gif via decoder");
                            if (ui_jimnjap_label) lv_obj_clear_flag(ui_jimnjap_label, LV_OBJ_FLAG_HIDDEN);
                        }
                    } else {
                        Serial.println("[GIF] Failed to load /sd_card/jimnjap.gif from SD");
                    }
                }
            }
        } else {
            gif.close();
        }

        current_gif_state = requested_gif_state;
        memset(canvas_buf, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t));

        // Load all GIFs into PSRAM for instant switching (executed after timer delay)
        if (!gif_idle)     gif_idle     = load_gif_to_psram(GIF_PATH_IDLE, &size_idle);
        if (!gif_medium)   gif_medium   = load_gif_to_psram(GIF_PATH_MEDIUM, &size_medium);
        if (!gif_max)      gif_max      = load_gif_to_psram(GIF_PATH_MAX, &size_max);
        if (!gif_redline)  gif_redline  = load_gif_to_psram(GIF_PATH_REDLINE, &size_redline);

        gif.begin(LITTLE_ENDIAN_PIXELS);
        bool ok = false;

        if (current_gif_state == GIF_STATE_IDLE && gif_idle && size_idle > 0) {
            ok = gif.open(gif_idle, (int)size_idle, GIFDraw);
            if (ui_godzilla_canvas) lv_img_set_zoom(ui_godzilla_canvas, 384); // 2% size increase
            Serial.printf("[GIF] Switched to IDLE (opened=%d)\n", (int)ok);
        } else if (current_gif_state == GIF_STATE_INCREASING && gif_medium && size_medium > 0) {
            ok = gif.open(gif_medium, (int)size_medium, GIFDraw);
            if (ui_godzilla_canvas) lv_img_set_zoom(ui_godzilla_canvas, 388); // 2% size reduction from 396
            Serial.printf("[GIF] Switched to MEDIUM (opened=%d)\n", (int)ok);
        } else if (current_gif_state == GIF_STATE_MAX && gif_max && size_max > 0) {
            ok = gif.open(gif_max, (int)size_max, GIFDraw);
            if (ui_godzilla_canvas) lv_img_set_zoom(ui_godzilla_canvas, 387); // 8% size increase (376 -> 387)
            Serial.printf("[GIF] Switched to MAX (opened=%d)\n", (int)ok);
        } else if (current_gif_state == GIF_STATE_REDLINE && gif_redline && size_redline > 0) {
            ok = gif.open(gif_redline, (int)size_redline, GIFDraw);
            if (ui_godzilla_canvas) lv_img_set_zoom(ui_godzilla_canvas, 380); // 1% size reduction from 384
            Serial.printf("[GIF] Switched to REDLINE (opened=%d)\n", (int)ok);
        }
    }

    // Determine if we have a valid open gif to play
    bool has_active_gif = (current_gif_state == GIF_STATE_IDLE && gif_idle) ||
                          (current_gif_state == GIF_STATE_INCREASING && gif_medium) ||
                          (current_gif_state == GIF_STATE_MAX && gif_max) ||
                          (current_gif_state == GIF_STATE_REDLINE && gif_redline);

    if (!has_active_gif) return;

    int delay = 0;
    if (gif.playFrame(false, &delay)) {
        lv_obj_invalidate(ui_godzilla_canvas);
        lv_timer_set_period(t, delay > 0 ? delay : 30);
    } else {
        gif.reset(); // loop
        lv_timer_set_period(t, 10);
    }
}

void set_gif_state(gif_state_t state) {
    if (requested_gif_state != state) {
        requested_gif_state = state;
    }
}


void ui_event_godzillaspeedometer(lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_SCREEN_LOAD_START) {
        // Reset state - GIFs open on demand from PSRAM
        current_gif_state = GIF_STATE_NONE;
        requested_gif_state = GIF_STATE_IDLE;

        // Draw static 1-frame GIF as placeholder for instant launch
        if (canvas_buf) {
            memset(canvas_buf, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t));
            if (!gif_still) {
                gif_still = load_gif_to_psram("/sd_card/speedometergif/rpm_still.gif", &size_still);
            }
            if (gif_still && size_still > 0) {
                gif.begin(LITTLE_ENDIAN_PIXELS);
                if (gif.open(gif_still, (int)size_still, GIFDraw)) {
                    int delay = 0;
                    gif.playFrame(false, &delay);
                    lv_obj_invalidate(ui_godzilla_canvas);
                    lv_img_set_zoom(ui_godzilla_canvas, 384); // Same specs as idle GIF (increased by 2%)
                    gif.close();
                }
            }
        }

        // Show Initializing text and hide jimnjap gif during the 700ms mock godzilla phase
        if (ui_jimnjap_label) lv_obj_clear_flag(ui_jimnjap_label, LV_OBJ_FLAG_HIDDEN);
        if (ui_jimnjap_canvas) lv_obj_add_flag(ui_jimnjap_canvas, LV_OBJ_FLAG_HIDDEN);
        if (ui_trailmaster_jap_img) lv_obj_add_flag(ui_trailmaster_jap_img, LV_OBJ_FLAG_HIDDEN);

        if (!gif_timer) {
            gif_timer = lv_timer_create(play_gif_timer_cb, 700, NULL); // 700ms delay as requested
        } else {
            lv_timer_set_period(gif_timer, 700);
            lv_timer_reset(gif_timer);
        }

        is_godzilla_animating = true;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ui_godzilla_rpm_arc);
        lv_anim_set_values(&a, 0, 8000);
        lv_anim_set_time(&a, 2200); 
        lv_anim_set_playback_time(&a, 1200); 
        lv_anim_set_delay(&a, 500); // 300ms shift transition + 200ms visual pause
        lv_anim_set_repeat_count(&a, 1);
        lv_anim_set_exec_cb(&a, godzilla_anim_cb);
        lv_anim_set_ready_cb(&a, godzilla_anim_ready_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    if (event_code == LV_EVENT_SCREEN_UNLOAD_START) {
        if (gif_timer) {
            lv_timer_del(gif_timer);
            gif_timer = NULL;
        }
        gif.close();
        
        // Only free static/one-time GIFs. Main GIFs remain persistent in PSRAM.
        if (gif_still) { heap_caps_free(gif_still); gif_still = NULL; size_still = 0; }
        if (jimnjap_gif_data) { heap_caps_free(jimnjap_gif_data); jimnjap_gif_data = NULL; size_jimnjap = 0; }
        
        current_gif_state = GIF_STATE_NONE;
    }

    if (event_code == LV_EVENT_LONG_PRESSED) {
        open_speedo_settings_menu(ui_godzillaspeedometer, true);
    }

    if (event_code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
            // Explicitly free GIFs to make memory available for the new screen BEFORE initializing it
            if (gif_timer) {
                lv_timer_del(gif_timer);
                gif_timer = NULL;
            }
            gif.close();
            // Main GIFs are no longer freed here; they are persistently cached.
            current_gif_state = GIF_STATE_NONE;

            lv_indev_wait_release(lv_indev_get_act());
            if (dir == LV_DIR_RIGHT) {
                _ui_screen_change(&ui_uispeedometer, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, &ui_uispeedometer_screen_init);
            } else {
                _ui_screen_change(&ui_uilauncher, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_uilauncher_screen_init);
            }
        }
    }
}

void ui_godzillaspeedometer_screen_init(void) {
    if (ui_godzillaspeedometer != NULL) return; 

    ui_godzillaspeedometer = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_godzillaspeedometer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_godzillaspeedometer, LV_OBJ_FLAG_CLICKABLE); // Clickable for long press detection
    lv_obj_set_style_bg_color(ui_godzillaspeedometer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_godzillaspeedometer, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 1. Mirrored RPM Arc - sweeps 180 degrees clockwise across the right side
    // Background angles: 150 to 330 (180 deg clockwise)
    // Mode NORMAL: Value fills from 150 clockwise to 330
    ui_godzilla_rpm_arc = lv_arc_create(ui_godzillaspeedometer);
    lv_obj_set_size(ui_godzilla_rpm_arc, 466, 466);
    lv_obj_align(ui_godzilla_rpm_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(ui_godzilla_rpm_arc, 0, 8000);
    lv_arc_set_value(ui_godzilla_rpm_arc, 0);
    lv_arc_set_bg_angles(ui_godzilla_rpm_arc, 150, 330);
    lv_arc_set_mode(ui_godzilla_rpm_arc, LV_ARC_MODE_NORMAL);

    lv_obj_set_style_arc_color(ui_godzilla_rpm_arc, lv_color_hex(0x00E5FF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_godzilla_rpm_arc, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_godzilla_rpm_arc, 12, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_godzilla_rpm_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(ui_godzilla_rpm_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ui_godzilla_rpm_arc, LV_OBJ_FLAG_CLICKABLE);

    // 2. GIF Canvas - 272x272, scaled by ~55% and positioned relative to bottom-left
    if (!canvas_buf) {
        canvas_buf = (lv_color_t *)heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (!canvas_buf) {
            canvas_buf = (lv_color_t *)malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t));
        }
    }
    
    ui_godzilla_canvas = lv_canvas_create(ui_godzillaspeedometer);
    if (canvas_buf) {
        lv_canvas_set_buffer(ui_godzilla_canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(ui_godzilla_canvas, lv_color_black(), LV_OPA_COVER);
    }
    // Shifted towards right (x: 61) and shifted 5% screen height up (y: 42)
    lv_obj_align(ui_godzilla_canvas, LV_ALIGN_BOTTOM_LEFT, 61, 42);
    // Set Godzilla GIF size scale to exactly 40% + 5% extra for idle (zoom 384)
    lv_img_set_zoom(ui_godzilla_canvas, 384);

    if (!jimnjap_canvas_buf) {
        jimnjap_canvas_buf = (lv_color_t *)heap_caps_malloc(JIMNJAP_WIDTH * JIMNJAP_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (!jimnjap_canvas_buf) {
            jimnjap_canvas_buf = (lv_color_t *)malloc(JIMNJAP_WIDTH * JIMNJAP_HEIGHT * sizeof(lv_color_t));
        }
    }
    ui_jimnjap_canvas = lv_canvas_create(ui_godzillaspeedometer);
    if (jimnjap_canvas_buf) {
        lv_canvas_set_buffer(ui_jimnjap_canvas, jimnjap_canvas_buf, JIMNJAP_WIDTH, JIMNJAP_HEIGHT, LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED);
        lv_canvas_fill_bg(ui_jimnjap_canvas, lv_color_hex(0x00FF00), LV_OPA_COVER);
    }
    // Placed between graduation "5" and godzilla.
    lv_obj_align(ui_jimnjap_canvas, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_flag(ui_jimnjap_canvas, LV_OBJ_FLAG_HIDDEN);

    extern const lv_img_dsc_t ui_img_trailmaster_jap;
    ui_trailmaster_jap_img = lv_img_create(ui_godzillaspeedometer);
    lv_img_set_src(ui_trailmaster_jap_img, &ui_img_trailmaster_jap);
    lv_obj_align(ui_trailmaster_jap_img, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_img_recolor(ui_trailmaster_jap_img, lv_color_hex(0xAAAAAA), LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(ui_trailmaster_jap_img, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_trailmaster_jap_img, LV_OBJ_FLAG_HIDDEN);

    ui_jimnjap_label = lv_label_create(ui_godzillaspeedometer);
    lv_label_set_text(ui_jimnjap_label, "Initializing");
    lv_obj_set_style_text_color(ui_jimnjap_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_jimnjap_label, &lv_font_montserrat_20, 0);
    lv_obj_align(ui_jimnjap_label, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_flag(ui_jimnjap_label, LV_OBJ_FLAG_HIDDEN);

    // 3. Digital Speed Value Label - aligned with 3 o'clock (right-middle), with comfortable space below 8 graduation
    ui_godzilla_speed_label = lv_label_create(ui_godzillaspeedometer);
    lv_obj_set_width(ui_godzilla_speed_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_godzilla_speed_label, LV_SIZE_CONTENT);
    lv_obj_align(ui_godzilla_speed_label, LV_ALIGN_RIGHT_MID, -20, 20);
    lv_label_set_text(ui_godzilla_speed_label, "0");
    lv_obj_set_style_text_color(ui_godzilla_speed_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_godzilla_speed_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_godzilla_speed_label, &ui_font_rajdhani200, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 4. Create 8 shadow labels to form a thick 2-pixel black stroke around the main speed value
    const int offsets[8][2] = {
        {-2, -2}, {0, -2}, {2, -2},
        {-2, 0},           {2, 0},
        {-2, 2},  {0, 2},  {2, 2}
    };
    for (int i = 0; i < 8; i++) {
        ui_godzilla_speed_label_shadows[i] = lv_label_create(ui_godzillaspeedometer);
        lv_obj_set_width(ui_godzilla_speed_label_shadows[i], LV_SIZE_CONTENT);
        lv_obj_set_height(ui_godzilla_speed_label_shadows[i], LV_SIZE_CONTENT);
        lv_obj_align_to(ui_godzilla_speed_label_shadows[i], ui_godzilla_speed_label, LV_ALIGN_CENTER, offsets[i][0], offsets[i][1]);
        lv_label_set_text(ui_godzilla_speed_label_shadows[i], "0");
        lv_obj_set_style_text_color(ui_godzilla_speed_label_shadows[i], lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_godzilla_speed_label_shadows[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_godzilla_speed_label_shadows[i], &ui_font_rajdhani200, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // 5. Create "km/h" unit label - aligned bottom right below the speed label using ui_font_rajdhani1
    ui_godzilla_unit_label = lv_label_create(ui_godzillaspeedometer);
    lv_obj_set_width(ui_godzilla_unit_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_godzilla_unit_label, LV_SIZE_CONTENT);
    lv_obj_align(ui_godzilla_unit_label, LV_ALIGN_RIGHT_MID, -30, 110); // Align below speed label, end matches speed value
    lv_label_set_text(ui_godzilla_unit_label, "km/h");
    lv_obj_set_style_text_color(ui_godzilla_unit_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_godzilla_unit_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_godzilla_unit_label, &ui_font_rajdhani1, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 6. Create 8 shadow labels for "km/h" outline
    for (int i = 0; i < 8; i++) {
        ui_godzilla_unit_label_shadows[i] = lv_label_create(ui_godzillaspeedometer);
        lv_obj_set_width(ui_godzilla_unit_label_shadows[i], LV_SIZE_CONTENT);
        lv_obj_set_height(ui_godzilla_unit_label_shadows[i], LV_SIZE_CONTENT);
        lv_obj_align_to(ui_godzilla_unit_label_shadows[i], ui_godzilla_unit_label, LV_ALIGN_CENTER, offsets[i][0], offsets[i][1]);
        lv_label_set_text(ui_godzilla_unit_label_shadows[i], "km/h");
        lv_obj_set_style_text_color(ui_godzilla_unit_label_shadows[i], lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_godzilla_unit_label_shadows[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_godzilla_unit_label_shadows[i], &ui_font_rajdhani1, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    build_godzilla_rpm_gauge(ui_godzillaspeedometer);

    lv_obj_add_event_cb(ui_godzillaspeedometer, ui_event_godzillaspeedometer, LV_EVENT_ALL, NULL);
    
    // Z-Order: Move Godzilla Canvas to the background so the arc and graduations are rendered above him, and Speed Label is in front
    lv_obj_move_background(ui_godzilla_canvas);
    for (int i = 0; i < 8; i++) {
        if (ui_godzilla_speed_label_shadows[i]) {
            lv_obj_move_foreground(ui_godzilla_speed_label_shadows[i]);
        }
    }
    lv_obj_move_foreground(ui_godzilla_speed_label);
    for (int i = 0; i < 8; i++) {
        if (ui_godzilla_unit_label_shadows[i]) {
            lv_obj_move_foreground(ui_godzilla_unit_label_shadows[i]);
        }
    }
    lv_obj_move_foreground(ui_godzilla_unit_label);

    // Ensure jimnjap items are always on top of gauge lines and godzilla
    if (ui_jimnjap_canvas) lv_obj_move_foreground(ui_jimnjap_canvas);
    if (ui_trailmaster_jap_img) lv_obj_move_foreground(ui_trailmaster_jap_img);
    if (ui_jimnjap_label) lv_obj_move_foreground(ui_jimnjap_label);
    
    // Pagination Dot Indicator (Right Active - 2 Dots, shifted down from -25 to -12)
    lv_obj_t * pag_dots_container = lv_obj_create(ui_godzillaspeedometer);
    lv_obj_set_size(pag_dots_container, 60, 20);
    lv_obj_align(pag_dots_container, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_opa(pag_dots_container, 0, 0);
    lv_obj_set_style_border_width(pag_dots_container, 0, 0);
    lv_obj_set_style_pad_all(pag_dots_container, 0, 0);
    lv_obj_clear_flag(pag_dots_container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * dot_std = lv_obj_create(pag_dots_container);
    lv_obj_set_size(dot_std, 8, 8);
    lv_obj_align(dot_std, LV_ALIGN_CENTER, -10, 0);
    lv_obj_set_style_radius(dot_std, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_std, lv_color_hex(0x444444), 0); // Dimmed
    lv_obj_set_style_border_width(dot_std, 0, 0);
    lv_obj_clear_flag(dot_std, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * dot_godz = lv_obj_create(pag_dots_container);
    lv_obj_set_size(dot_godz, 8, 8);
    lv_obj_align(dot_godz, LV_ALIGN_CENTER, 10, 0);
    lv_obj_set_style_radius(dot_godz, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_godz, lv_color_hex(0xFF9500), 0); // Active
    lv_obj_set_style_border_width(dot_godz, 0, 0);
    lv_obj_clear_flag(dot_godz, LV_OBJ_FLAG_CLICKABLE);
}

void ui_godzillaspeedometer_screen_destroy(void) {
    if (ui_godzillaspeedometer) {
        lv_obj_del(ui_godzillaspeedometer);
    }
    ui_godzillaspeedometer = NULL;
    ui_godzilla_canvas = NULL;
    
    // Free the canvas buffer memory to prevent memory leaks and PSRAM fragmentation
    // which was causing the game menu to render fuzzily due to OOM
    if (canvas_buf) {
        heap_caps_free(canvas_buf);
        canvas_buf = NULL;
    }
    if (jimnjap_canvas_buf) {
        heap_caps_free(jimnjap_canvas_buf);
        jimnjap_canvas_buf = NULL;
    }
    ui_jimnjap_canvas = NULL;
    ui_trailmaster_jap_img = NULL;
    ui_jimnjap_label = NULL;
    ui_godzilla_speed_label = NULL;
    for (int i = 0; i < 8; i++) ui_godzilla_speed_label_shadows[i] = NULL;
    ui_godzilla_unit_label = NULL;
    for (int i = 0; i < 8; i++) ui_godzilla_unit_label_shadows[i] = NULL;
    ui_godzilla_rpm_arc = NULL;
    ui_godzilla_x1000_label = NULL;
    for (int i = 0; i < 41; i++) godzilla_tick_lines[i] = NULL;
    for (int i = 0; i < 9; i++) godzilla_tick_labels[i] = NULL;
}
