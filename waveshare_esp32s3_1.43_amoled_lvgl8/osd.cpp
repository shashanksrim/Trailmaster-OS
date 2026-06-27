#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "hw_config.h"
#include "tft_driver.h"
#include "turning.h"

#if ENABLE_SOUND
#include <driver/i2s.h>
#endif

#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>

extern "C" {
#include "noftypes.h"
#include "bitmap.h"
#include "osd.h"
#include "nofrendo.h"
#include "NesEngine.h"
#include "nesinput.h"
#include "event.h"
#include "nofconfig.h"
}

#define NES_SCREEN_WIDTH 256
#define NES_SCREEN_HEIGHT 240
#define AUDIO_SAMPLE_RATE 16000
 
int master_volume = 100;
bool show_fps = false;
bool select_pressed = false;
static bool runtime_sound_enabled = false;

extern Amoled amoled; 
extern "C" int nes_get_gamepad_state();

// --- SHARED VIDEO BUFFERS ---
static uint16_t myPalette565_swapped[256];
static bitmap_t *game_bitmap = NULL;
static bool video_ready = false;
static uint16_t *frame_buffer = NULL;  
static uint16_t *scaled_buffer = NULL;
static unsigned long next_frame_ms = 0;

nesinput_t joypad_p1;
nesinput_t joypad_p2;
static unsigned long rom_load_time = 0;

static int16_t mono_buffer[512];
static void (*emulator_audio_callback)(void *buffer, int length) = NULL;

esp_timer_handle_t nes_timer_handle = NULL;
void (*emu_timer_callback)(void) = NULL;

void IRAM_ATTR timer_callback_handler(void *arg) {
  if (emu_timer_callback) emu_timer_callback();
}
 
extern char *global_rom_data;
extern int global_rom_size;
static FILE* rom_stream_file = NULL;

extern "C" {
    int osd_rom_open(const char *path);
    int osd_rom_read(void *dst, int len);
    void osd_rom_close(void);
    bool vid_preload_rom(const char *path);
}

// --- VIDEO CORE ---

extern "C" void osd_setpalette(rgb_t *pal) {
  if (!pal) return;
  for (int i = 0; i < 256; i++) {
    uint16_t r = (pal[i].r >> 3) & 0x1F;
    uint16_t g = (pal[i].g >> 2) & 0x3F;
    uint16_t b = (pal[i].b >> 3) & 0x1F;
    uint16_t c = (r << 11) | (g << 5) | b;
    myPalette565_swapped[i] = __builtin_bswap16(c);
  }
}

extern "C" void IRAM_ATTR osd_blit(bitmap_t *bmp) {
  if (!video_ready || !bmp || !bmp->line || !bmp->line[0] || !frame_buffer) return;
  for (int y = 0; y < NES_SCREEN_HEIGHT; y++) {
    uint8_t *src = bmp->line[y];
    uint16_t *dst = &frame_buffer[y * NES_SCREEN_WIDTH];
    for (int x = 0; x < NES_SCREEN_WIDTH; x++) {
      dst[x] = myPalette565_swapped[src[x]];
    }
  }
}

extern "C" void input_register(nesinput_t *input);

extern "C" int osd_init(void) {
  Serial.println("[DEBUG] osd_init: ENTER");
  
  joypad_p1.type = INP_JOYPAD0;
  joypad_p1.data = 0;
  input_register(&joypad_p1);
  
  joypad_p2.type = INP_JOYPAD1;
  joypad_p2.data = 0;
  input_register(&joypad_p2);
  
  return 0;
}

static bool first_frame = true;

extern "C" int vid_init(int width, int height, viddriver_t *osd_driver) {
  Serial.printf("[DEBUG] vid_init: START (%dx%d)\n", width, height);
  if (!game_bitmap) game_bitmap = bmp_create(NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, 0); 
  
  Serial.printf("[DEBUG] Video Alloc. Heap: %u, PSRAM: %u\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  if (!frame_buffer) frame_buffer = (uint16_t *)heap_caps_malloc(256 * 240 * 2, MALLOC_CAP_SPIRAM);
  if (!scaled_buffer) scaled_buffer = (uint16_t *)heap_caps_malloc(466 * 466 * 2, MALLOC_CAP_SPIRAM);
  
  if (!frame_buffer || !scaled_buffer) {
      Serial.println("[OSD] CRITICAL: BUFFER FAIL!");
      return -1;
  }
  
  memset(scaled_buffer, 0, 466 * 466 * 2);
  video_ready = true;
  next_frame_ms = millis();
  first_frame = true; // Reset for new game launch!
  Serial.println("[DEBUG] vid_init: SUCCESS");
  return 0; 
}

extern "C" void nes_notify_first_frame_rendered();

extern "C" void vid_flush(void) {
    if (first_frame) { 
        Serial.println("[DEBUG] vid_flush: FIRST FRAME REACHED"); 
        nes_notify_first_frame_rendered();
        first_frame = false; 
    }

    if (!NesEngine::is_running || !video_ready) return;
    
    uint32_t now = millis();
    if (now < next_frame_ms) { delay(next_frame_ms - now); }
    next_frame_ms += 28; 
    if (now > next_frame_ms + 100) next_frame_ms = now;

    osd_blit(game_bitmap);

    const int v_off = 35; 
    for (int y = 0; y < 466; y++) {
        uint16_t* dest_row = &scaled_buffer[y * 466];
        if (y < v_off || y >= (466 - v_off)) {
            memset(dest_row, 0, 466 * 2);
        } else {
            int src_y = ((y - v_off) * 240) / (466 - (v_off * 2));
            if (src_y >= 240) src_y = 239;
            uint16_t* src_row = &frame_buffer[src_y * 256];
            for (int x = 0; x < 466; x++) {
                int src_x = (x * 256) / 466;
                if (src_x >= 256) src_x = 255;
                dest_row[x] = src_row[src_x];
            }
        }
    }

    const int chunk_height = 10;
    for (int y_chunk = 0; y_chunk < 466; y_chunk += chunk_height) {
        int h = chunk_height;
        if (y_chunk + h > 466) h = 466 - y_chunk;
        amoled.drawArea(0, y_chunk, 465, y_chunk + h - 1, &scaled_buffer[y_chunk * 466]);
    }
    vTaskDelay(1);
}

// --- SYSTEM STUBS & ROM LOAD ---

extern "C" int osd_rom_open(const char *filename) {
  Serial.printf("[DEBUG] ROM Discovery: Opening %s\n", filename);
  rom_stream_file = fopen(filename, "rb");
  if (rom_stream_file) Serial.println("[DEBUG] ROM Discovery: SUCCESS");
  return (rom_stream_file != NULL) ? 0 : -1;
}

extern "C" int osd_rom_read(void *buf, int size) {
  if (rom_stream_file == NULL) return 0;
  static int total_read = 0;
  int read = fread(buf, 1, size, rom_stream_file);
  if (read > 0) total_read += read;
  
  // Heartbeat every 16KB to satisfy watchdog and show progress
  if (total_read % 16384 == 0 && read > 0) {
      Serial.printf("[DEBUG] ROM Load: %d bytes\n", total_read);
      vTaskDelay(1); 
  }
  return read;
}

extern "C" void osd_rom_close(void) {
  if (rom_stream_file) { fclose(rom_stream_file); rom_stream_file = NULL; }
}

extern "C" bool vid_preload_rom(const char *path) {
  if (global_rom_data) { heap_caps_free(global_rom_data); global_rom_data = NULL; }
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  global_rom_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  global_rom_data = (char *)heap_caps_malloc(global_rom_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!global_rom_data) { fclose(f); return false; }
  fread(global_rom_data, 1, global_rom_size, f);
  fclose(f);
  return true;
}

extern "C" void osd_getinput(void) {
  int hw = nes_get_gamepad_state();
  int nes_data = 0;
  if (hw & 0x01) nes_data |= 0x01; // A
  if (hw & 0x02) nes_data |= 0x02; // B
  if (hw & 0x04) nes_data |= 0x04; // SELECT
  if (hw & 0x08) nes_data |= 0x08; // START
  if (hw & 0x10) nes_data |= 0x10; // UP
  if (hw & 0x20) nes_data |= 0x20; // DOWN
  if (hw & 0x40) nes_data |= 0x40; // LEFT
  if (hw & 0x80) nes_data |= 0x80; // RIGHT
  
  // Assign to handles so NES core sees them
  joypad_p1.data = nes_data;
  joypad_p2.data = nes_data;
}

extern "C" void osd_getmouse(int *x, int *y, int *buttons) {
  if (x) *x = 0;
  if (y) *y = 0;
  if (buttons) *buttons = 0;
}
extern "C" void input_shutdown(void);

extern "C" void osd_shutdown() {
    Serial.println("[SYSTEM] CLEAN EXIT: Purging Emulator State...");
    
    input_shutdown();

    if (nes_timer_handle) {
        esp_timer_stop(nes_timer_handle);
        esp_timer_delete(nes_timer_handle);
        nes_timer_handle = NULL;
    }
}
extern "C" int osd_installtimer(int freq, void *func, int func_param, void *func2, int func2_param) {
  emu_timer_callback = (void (*)(void))func;
  const esp_timer_create_args_t timer_args = { .callback = &timer_callback_handler, .name = "nes_timer" };
  esp_timer_create(&timer_args, &nes_timer_handle);
  int slow_freq = (freq * 6) / 10;
  esp_timer_start_periodic(nes_timer_handle, 1000000 / slow_freq);
  return 0;
}
extern "C" int osd_gettime(void) { return millis(); }
extern "C" void vid_shutdown() {}
extern "C" int vid_setmode(int width, int height) { return 0; }
extern "C" void vid_setpalette(rgb_t *pal) { osd_setpalette(pal); }
extern "C" bitmap_t *vid_getbuffer() { return game_bitmap; }
extern "C" void osd_getvideoinfo(vidinfo_t *info) { info->default_width = 256; info->default_height = 240; }
extern "C" void osd_togglefullscreen(int code) {}
extern "C" char *osd_newextension(char *string, char *ext) { return NULL; }
extern "C" void osd_fullname(char *fullname, const char *shortname) { strcpy(fullname, shortname); }
extern "C" int osd_main(int argc, char *argv[]) {
  return nofrendo_main(argc, argv);
}
extern "C" int nofrendo_log_init(void) { return 0; }
extern "C" void nofrendo_log_shutdown(void) {}
extern "C" int nofrendo_log_print(const char *s) { return 0; }
extern "C" int nofrendo_log_printf(const char *format, ...) { return 0; }
extern "C" void nofrendo_log_assert(int expr, int line, const char *file, char *msg) {}
extern "C" void osd_initvideo(int *lines) { *lines = 240; }
extern "C" void osd_shutdownvideo() {}
extern "C" void osd_setscreen(int x, int y, int width, int height) {}
extern "C" void osd_stopsound(void) {}
extern "C" void osd_writesound(void *stream, int len) {}
extern "C" void osd_getsoundinfo(sndinfo_t *info) {}
extern "C" void osd_setsound(void (*playfunc)(void *buffer, int length)) {}
