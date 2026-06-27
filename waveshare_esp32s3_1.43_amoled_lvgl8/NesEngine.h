#ifndef NES_ENGINE_H
#define NES_ENGINE_H

#include <Arduino.h>

class NesEngine {
public:
    static bool loadROM(const char* path);
    static void update();
    static bool is_running;
};

#ifdef __cplusplus
extern "C" {
#endif
bool nes_is_running();
void nes_set_running(bool val);
#ifdef __cplusplus
}
#endif

#endif
