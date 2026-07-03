// Day-2 L3 checkpoint: a trivial sketch exercising L0+L1+L2+L3 together
// before attempting the real Trailmaster .ino. Draws a color that changes
// on touch, proving Amoled (L1) + getTouch (L1) + Serial/millis (L2) +
// the SDL window/event pump (L3) all work as one pipeline.
#include "Arduino.h"
#include "amoled.h"
#include "FT3168.h"

void setup() {
    Serial.begin(115200);
    Serial.println("L3 checkpoint: setup() running");
    amoled.begin();
    Touch_Init();
    amoled.fillScreen(AMOLED_COLOR_NAVY);
}

void loop() {
    uint16_t x, y;
    if (getTouch(&x, &y)) {
        amoled.fillRect(x - 20, y - 20, 40, 40, AMOLED_COLOR_ORANGE);
    } else {
        amoled.fillScreen(AMOLED_COLOR_NAVY);
    }
    delay(16);
}
