#include "NesEngine.h"
#include <Arduino.h>
#include "nofrendo.h"
#include <string.h>

char current_rom_path[128];
bool NesEngine::is_running = false;

extern "C" bool nes_is_running() { return NesEngine::is_running; }
extern "C" void nes_set_running(bool val) { NesEngine::is_running = val; }
extern "C" void reset_nes_controller();

void nesTask(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(200)); // Let the system settle
    Serial.println("[NES] Task Started");
    Serial.printf("[NES] Initial Stack: %u\n", uxTaskGetStackHighWaterMark(NULL));
    
    // FORCE RESET: Digital Power Cycle
    main_reset_quit(); 
    main_eject();
    main_hard_reset(); 
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Run emulator using direct main_loop to bypass argv parsing
    Serial.println("[NES] Direct Ignition: Calling main_loop...");
    
    // WAKE FPU: Prevents Double Exception on first float op
    volatile float f_test = 1.0f; f_test *= 1.1f;
    Serial.printf("[NES] FPU Warm: %f\n", f_test);
    
    main_loop(current_rom_path, system_nes);

    Serial.printf("[NES] Stack at Exit: %u\n", uxTaskGetStackHighWaterMark(NULL));
    Serial.println("[NES] Emulator Exited");
    NesEngine::is_running = false;
    vTaskDelete(NULL);
}

bool NesEngine::loadROM(const char* path) {
    if (is_running) return false;
    
    // Clear old state
    memset(current_rom_path, 0, sizeof(current_rom_path));
    
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);

    is_running = true;
    reset_nes_controller();
    strncpy(current_rom_path, path, 127);
    
    Serial.printf("[NES] Launching: %s\n", current_rom_path);
    
    // 64KB stack, back to Core 1 (Worker Core) to avoid System Watchdog on Core 0
    BaseType_t ret = xTaskCreatePinnedToCore(nesTask, "NesTask", 65536, NULL, 5, NULL, 1);
    
    if (ret != pdPASS) {
        is_running = false;
        return false;
    }

    return true;
}
