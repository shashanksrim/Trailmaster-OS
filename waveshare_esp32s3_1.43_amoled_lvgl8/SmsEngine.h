#ifndef SMS_ENGINE_H
#define SMS_ENGINE_H

#include <Arduino.h>

#define J_UP      0x01
#define J_DOWN    0x02
#define J_LEFT    0x04
#define J_RIGHT   0x08
#define J_BUTTON1 0x10
#define J_BUTTON2 0x20

class SmsEngine {
public:
    static bool loadROM(const char* path);
    static void update();
    static void setInput(uint8_t input);
    static bool isRunning() { return is_running; }

private:
    static bool is_running;
};

#endif
