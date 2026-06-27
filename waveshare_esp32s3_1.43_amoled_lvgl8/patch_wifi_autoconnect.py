import sys

# Patch OTAManager.cpp
with open('OTAManager.cpp', 'r') as f:
    code = f.read()

old_ota = """    // Bring it back up in pure STA mode
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Wipe static IP
    WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress(), IPAddress());"""
new_ota = """    // Bring it back up in pure STA mode
    WiFi.mode(WIFI_STA);
    WiFi.setAutoConnect(false); // STOP ESP32 FROM IMMEDIATELY AUTO-CONNECTING TO OBD!
    delay(100);
    
    // Wipe static IP
    WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress());"""

if old_ota in code:
    code = code.replace(old_ota, new_ota)
else:
    print("Could not find OTA mode block in OTAManager.cpp")

with open('OTAManager.cpp', 'w') as f:
    f.write(code)

# Patch OBD worker
with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'r') as f:
    code = f.read()

old_obd = """        // Ensure WiFi Connection in Station Mode
        if (WiFi.status() != WL_CONNECTED) {"""
new_obd = """        // Ensure WiFi Connection in Station Mode
        if (WiFi.status() != WL_CONNECTED) {
            // Check one more time before committing to a 10s blocking WiFi attempt
            if (ota_st->state != OTA_IDLE) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }"""

if old_obd in code:
    code = code.replace(old_obd, new_obd)
else:
    print("Could not find OBD block in ino")

with open('waveshare_esp32s3_1.43_amoled_lvgl8.ino', 'w') as f:
    f.write(code)

print("Patch applied.")
