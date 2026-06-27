import sys

# Patch OTAManager.cpp
with open('OTAManager.cpp', 'r') as f:
    code = f.read()

old_ota = """    // Switch to dual AP+STA mode — preserves the existing photo-upload AP
    WiFi.mode(WIFI_AP_STA);"""
new_ota = """    // Shut down the AP entirely and use pure STA mode so the ESP32 can switch channels!
    // In dual AP+STA mode, the ESP32 forces the STA to match the AP's channel (Channel 1),
    // which prevents it from connecting to home networks on other channels.
    extern bool wifi_ap_running;
    if (wifi_ap_running) {
        WiFi.softAPdisconnect(true);
        wifi_ap_running = false;
    }
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);"""

if old_ota in code:
    code = code.replace(old_ota, new_ota)
else:
    print("Could not find OTA mode block in OTAManager.cpp")

with open('OTAManager.cpp', 'w') as f:
    f.write(code)

# Patch PhotoFrameApp.cpp
with open('PhotoFrameApp.cpp', 'r') as f:
    code = f.read()

old_html = "<input type='password' name='pass' placeholder='Password'>"
new_html = "<input type='text' name='pass' placeholder='Password'>"

if old_html in code:
    code = code.replace(old_html, new_html)
else:
    print("Could not find password input in PhotoFrameApp.cpp")

with open('PhotoFrameApp.cpp', 'w') as f:
    f.write(code)

print("Patch applied.")
