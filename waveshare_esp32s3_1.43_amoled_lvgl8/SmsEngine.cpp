#include "SmsEngine.h"
#include "amoled.h"
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

extern "C" {
#include "shared.h"
}

extern Amoled amoled;

bool SmsEngine::is_running = false;
static uint8_t* rom_ptr = nullptr;
static uint8_t* v_data = nullptr;
static uint16_t* out_fb = nullptr;
static uint8_t* dummy_buf = nullptr;

// OSD functions required by smsplus
extern "C" {
    void system_load_sram(void) {}
    void system_save_sram(void) {}
    
    char unalChar(const char *adr) {
        return *adr;
    }
}

bool SmsEngine::loadROM(const char* path) {
    Serial.print("SmsEngine: Loading "); Serial.println(path);
    is_running = false;
    
    if (rom_ptr) { free(rom_ptr); rom_ptr = nullptr; }
    
    FILE* f = fopen(path, "rb");
    if (!f) { Serial.println("SmsEngine: Failed to open file"); return false; }
    
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    Serial.println("SmsEngine: Allocating ROM (1MB safe size)..."); Serial.flush();
    // Always allocate 1MB to avoid mapper out-of-bounds
    rom_ptr = (uint8_t*)heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);
    if (!rom_ptr) {
        Serial.println("SmsEngine: Failed to allocate ROM buffer in PSRAM!"); Serial.flush();
        fclose(f); return false; 
    }
    memset(rom_ptr, 0xFF, 1024 * 1024);
    
    Serial.println("SmsEngine: Reading ROM file..."); Serial.flush();
    fread(rom_ptr, 1, size, f);
    fclose(f);
    Serial.println("SmsEngine: ROM Read OK"); Serial.flush();
    delay(200); // Wait for Serial to clear and SD to settle
    
    if (!v_data) {
        Serial.print("SmsEngine: Alloc v_data... "); Serial.flush();
        // Allocate with 1 line of padding at start and end
        v_data = (uint8_t*)heap_caps_malloc(256 * 262 + 512, MALLOC_CAP_SPIRAM);
        Serial.println(v_data ? "OK" : "FAIL"); Serial.flush();
    }
    if (!out_fb) {
        Serial.print("SmsEngine: Alloc out_fb... "); Serial.flush();
        out_fb = (uint16_t*)heap_caps_malloc(256 * 262 * 2, MALLOC_CAP_SPIRAM);
        Serial.println(out_fb ? "OK" : "FAIL"); Serial.flush();
    }
    if (!dummy_buf) {
        Serial.print("SmsEngine: Alloc dummy... "); Serial.flush();
        dummy_buf = (uint8_t*)malloc(0x2000); 
        Serial.println(dummy_buf ? "OK" : "FAIL"); Serial.flush();
    }

    if (!v_data || !out_fb || !dummy_buf) {
        Serial.printf("SmsEngine: Buffer fail: v_data=%p, out_fb=%p, dummy=%p\n", v_data, out_fb, dummy_buf); Serial.flush();
        return false;
    }
    Serial.println("SmsEngine: Buffers OK"); Serial.flush();
    Serial.println("SmsEngine: Buffers OK"); Serial.flush();

    bitmap.data = v_data + 256; // Start at second line (padding)
    bitmap.width = 256;
    bitmap.height = 192;
    bitmap.pitch = 256;
    bitmap.depth = 8;
    
    cart.rom = (uint8*)rom_ptr;
    cart.pages = (size + 0x3FFF) / 0x4000;
    cart.type = TYPE_SMS;
    
    sms.dummy = dummy_buf;
    sms.use_fm = 0;
    sms.country = TYPE_OVERSEAS;
    
    emu_system_init(0);
    system_reset();
    
    amoled.fillScreen(0x0000);
    is_running = true;
    Serial.println("SmsEngine: Emulator Started Successfully");
    return true;
}

void SmsEngine::update() {
    if (!is_running) return;
    
    static int frame_count = 0;
    if (frame_count < 100) {
        Serial.printf("SmsEngine: Starting Frame %d\n", frame_count++); Serial.flush();
    }
    
    // Safety yield for background tasks
    vTaskDelay(1); 
    
    sms_frame(1); // SKIP RENDER TEST
    
    if (frame_count <= 100) {
        Serial.printf("SmsEngine: Finished Frame %d. Converting...\n", frame_count-1); Serial.flush();
    }
    
    uint16_t pal565[32];
    for (int i = 0; i < 32; i++) {
        uint8_t r = bitmap.pal.color[i][0];
        uint8_t g = bitmap.pal.color[i][1];
        uint8_t b = bitmap.pal.color[i][2];
        pal565[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    
    // Batch convert pixels from the correct offset (skipping padding)
    for (int i = 0; i < 256 * 192; i++) {
        uint16_t color = pal565[bitmap.data[i] & 31];
        out_fb[i] = (color << 8) | (color >> 8); 
    }
    
    int start_x = (466 - 256) / 2;
    int start_y = (466 - 192) / 2;
    
    if (frame_count <= 100) {
        Serial.println("SmsEngine: Pushing to Display..."); Serial.flush();
    }
    
    // Draw the entire frame in ONE transaction for maximum speed
    amoled.drawArea(start_x, start_y, start_x + 255, start_y + 191, out_fb);

    if (frame_count <= 100) {
        Serial.println("SmsEngine: Display OK"); Serial.flush();
    }
}

void SmsEngine::setInput(uint8_t joy) {
    input.pad[0] = 0;
    if (joy & J_UP)    input.pad[0] |= INPUT_UP;
    if (joy & J_DOWN)  input.pad[0] |= INPUT_DOWN;
    if (joy & J_LEFT)  input.pad[0] |= INPUT_LEFT;
    if (joy & J_RIGHT) input.pad[0] |= INPUT_RIGHT;
    if (joy & J_BUTTON1) input.pad[0] |= INPUT_BUTTON1;
    if (joy & J_BUTTON2) input.pad[0] |= INPUT_BUTTON2;
}
