// No-op stand-ins for the firmware app callbacks the SquareLine UI references,
// so the generated screens link and render in the simulator.
#include "lvgl.h"
#include <stdbool.h>

// Hand-coded speedometer bits referenced by ui_uispeedometer.c. The pure-LVGL
// parts (gauge, ticks, settings menu) come from the REAL shared header
// godzilla_speedo_ui.h (included by sim_screens.cpp) — only the genuinely
// firmware-only screen lifecycle is stubbed here.
lv_obj_t *ui_godzillaspeedometer = NULL;
void ui_godzillaspeedometer_screen_init(void) { }  // sim builds its own Godzilla screen (sim_screens.cpp)

// Globals the shared godzilla_speedo_ui.h functions read/write.
lv_obj_t *ui_godzilla_canvas = NULL;
lv_obj_t *ui_godzilla_speed_label = NULL;
lv_obj_t *ui_godzilla_speed_label_shadows[8] = {0};
lv_obj_t *ui_godzilla_unit_label = NULL;
lv_obj_t *ui_godzilla_unit_label_shadows[8] = {0};
lv_obj_t *ui_godzilla_rpm_arc = NULL;
lv_obj_t *godzilla_tick_lines[41] = {0};
lv_obj_t *godzilla_tick_labels[9] = {0};
lv_obj_t *ui_godzilla_x1000_label = NULL;
bool is_godzilla_animating = false;
int default_speedometer = 0;
bool is_simulating_obd = false;

void save_speedo_preferences(int val) { default_speedometer = val; }

void app_about(lv_event_t *e)            { (void)e; }
void app_dino_jump_trigger(lv_event_t *e){ (void)e; }
void app_imageframe(lv_event_t *e)       { (void)e; }
void app_settings(lv_event_t *e)         { (void)e; }
void app_start_dino_game(lv_event_t *e)  { (void)e; }
void app_zero_inclinometer(lv_event_t *e){ (void)e; }
void build_rom_menu(void)                { }
void exit_launcher(void)                 { }
void stop_all_games(void)                { }
void switch_to_launcher(void)            { }
