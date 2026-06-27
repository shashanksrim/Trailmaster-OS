#ifndef UI_GODZILLASPEEDOMETER_H
#define UI_GODZILLASPEEDOMETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ui.h"

// SCREEN: ui_godzillaspeedometer
extern void ui_godzillaspeedometer_screen_init(void);
extern void ui_godzillaspeedometer_screen_destroy(void);
extern void ui_event_godzillaspeedometer(lv_event_t * e);

extern lv_obj_t *ui_godzillaspeedometer;
extern lv_obj_t *ui_godzilla_canvas;
extern lv_obj_t *ui_godzilla_speed_label;
extern lv_obj_t *ui_godzilla_rpm_arc;
extern lv_obj_t *godzilla_tick_lines[41];
extern lv_obj_t *godzilla_tick_labels[9];
extern lv_obj_t *ui_godzilla_x1000_label;
extern bool is_godzilla_animating;

extern int default_speedometer;
extern bool is_simulating_obd;

void update_godzilla_ticks_color(bool all_red);
void load_speedo_preferences(void);
void save_speedo_preferences(int val);
void open_speedo_settings_menu(lv_obj_t * parent_screen, bool is_godzilla);
void ui_godzilla_speed_update(int speed, lv_color_t color);
uint16_t map_rpm_to_arc_value(uint16_t rpm);

typedef enum {
    GIF_STATE_NONE,
    GIF_STATE_IDLE,
    GIF_STATE_INCREASING,
    GIF_STATE_MAX,
    GIF_STATE_REDLINE
} gif_state_t;

void set_gif_state(gif_state_t state);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // UI_GODZILLASPEEDOMETER_H
