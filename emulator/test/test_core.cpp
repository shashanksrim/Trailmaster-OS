// Day-1 checkpoint: proves L2 (Arduino-core shim) compiles and behaves
// correctly standalone, before L1 (board BSP) is written. Mimics what a
// trivial sketch's setup()/loop() would do.
#include "Arduino.h"
#include "Preferences.h"

void setup() {
    Serial.begin(115200);
    Serial.println("L2 core shim: setup() running");
    Serial.printf("millis() at boot = %u\n", millis());

    Preferences prefs;
    prefs.begin("test", false);
    prefs.putInt("counter", 42);
    Serial.printf("Preferences round-trip: counter = %d (expect 42)\n", prefs.getInt("counter", -1));
    prefs.putString("name", "trailmaster");
    Serial.printf("Preferences string: name = %s (expect trailmaster)\n", prefs.getString("name", "?").c_str());
    prefs.end();
}

void loop() {
    static int n = 0;
    Serial.printf("loop() tick %d at millis()=%u\n", n, millis());
    delay(200);
    if (++n >= 3) {
        Serial.println("L2 checkpoint PASSED");
        std::exit(0);
    }
}

int main() {
    setup();
    while (true) loop();
}
