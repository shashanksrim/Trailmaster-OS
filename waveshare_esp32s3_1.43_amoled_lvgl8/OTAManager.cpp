#include "OTAManager.h"
#include "version.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include "sd_card_bsp.h"
#include <esp_wifi.h>
#include "ota_logic.h"   // version_is_newer(), json_get_str() — also unit-tested on host

// ── Config ────────────────────────────────────────────────────────────────────
// This URL points to docs/version.json on your GitHub Pages site.
// Update this to match your actual GitHub username / repo name.
#define OTA_VERSION_URL  "https://raw.githubusercontent.com/shashanksrim/Trailmaster-OS/main/version.json"
#define OTA_NVS_NS       "ota_wifi"
#define OTA_MAX_NETWORKS 8
#define OTA_WIFI_TIMEOUT_MS 15000

// ── State ─────────────────────────────────────────────────────────────────────
static OTAStatus s_status;
static bool      s_install_requested = false;
static char      s_pending_fw_url[256];
static TaskHandle_t s_task_handle = NULL;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void set_status(OTAState state, int progress, const char* text) {
    s_status.state    = state;
    s_status.progress = progress;
    strncpy(s_status.status_text, text, sizeof(s_status.status_text) - 1);
    s_status.status_text[sizeof(s_status.status_text) - 1] = '\0';
    Serial.printf("[OTA] %s\n", text);
}

// ── WiFi Network Storage ──────────────────────────────────────────────────────
void ota_init() {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_IDLE;
    strncpy(s_status.status_text, "Tap to check for updates", sizeof(s_status.status_text) - 1);
}

void ota_add_network(const char* ssid, const char* password) {
    Preferences prefs;
    prefs.begin(OTA_NVS_NS, false);

    // Find existing slot for this SSID or a free slot
    for (int i = 0; i < OTA_MAX_NETWORKS; i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);

        String existing = prefs.getString(key_ssid, "");
        if (existing == "" || existing == ssid) {
            prefs.putString(key_ssid, ssid);
            prefs.putString(key_pass, password);
            Serial.printf("[OTA] Saved network: %s (slot %d)\n", ssid, i);
            break;
        }
    }
    prefs.end();
}

void ota_remove_network(const char* ssid) {
    Preferences prefs;
    prefs.begin(OTA_NVS_NS, false);
    for (int i = 0; i < OTA_MAX_NETWORKS; i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
        if (prefs.getString(key_ssid, "") == ssid) {
            prefs.remove(key_ssid);
            prefs.remove(key_pass);
            Serial.printf("[OTA] Removed network: %s\n", ssid);
            break;
        }
    }
    prefs.end();
}

int ota_list_networks(char ssids[][33], int max_count) {
    Preferences prefs;
    prefs.begin(OTA_NVS_NS, true);
    int count = 0;
    for (int i = 0; i < OTA_MAX_NETWORKS && count < max_count; i++) {
        char key_ssid[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        String s = prefs.getString(key_ssid, "");
        if (s != "") {
            strncpy(ssids[count], s.c_str(), 32);
            ssids[count][32] = '\0';
            count++;
        }
    }
    prefs.end();
    return count;
}

// ── WiFi Connect ──────────────────────────────────────────────────────────────
static bool connect_to_known_network() {
    set_status(OTA_SCANNING_WIFI, 0, "Scanning for WiFi networks...");

    // Shut down the AP entirely and use pure STA mode so the ESP32 can switch channels!
    // In dual AP+STA mode, the ESP32 forces the STA to match the AP's channel (Channel 1),
    // which prevents it from connecting to home networks on other channels.
    extern bool wifi_ap_running;
    if (wifi_ap_running) {
        WiFi.softAPdisconnect(true);
        wifi_ap_running = false;
    }
    
    // Force a hardware-level Wi-Fi reset to nuke the OBD task's ongoing connection attempts
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(250);
    
    // Bring it back up in pure STA mode
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // STOP ESP32 FROM IMMEDIATELY AUTO-CONNECTING TO OBD!
    
    // Re-enable 802.11n protocol support so we can connect to modern home routers
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
    delay(100);
    
    // Wipe static IP (revert to DHCP)
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    delay(100);

    // --- DIAGNOSTIC SCAN ---
    Serial.println("[OTA] Executing deep diagnostic scan...");
    int n = WiFi.scanNetworks(false, false);
    if (n <= 0) {
        Serial.println("[OTA] No networks found in diagnostic scan!");
    } else {
        for (int i = 0; i < n; i++) {
            Serial.printf("[OTA] Scan saw: '%s' (RSSI: %d, Ch: %d)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
        }
    }
    // -----------------------

    Preferences prefs;
    prefs.begin(OTA_NVS_NS, true);
    
    bool found_any_saved = false;
    Serial.println("[OTA] Attempting to connect to saved WiFi networks...");

    // Try to connect to each saved network directly
    for (int i = 0; i < OTA_MAX_NETWORKS; i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
        String saved_ssid = prefs.getString(key_ssid, "");
        if (saved_ssid == "") continue;

        found_any_saved = true;
        String saved_pass = prefs.getString(key_pass, "");

        char buf[64];
        snprintf(buf, sizeof(buf), "Connecting to %s...", saved_ssid.c_str());
        set_status(OTA_CONNECTING_WIFI, 10, buf);
        
        Serial.printf("[OTA] Trying network %d: '%s' with password '%s'\n", i, saved_ssid.c_str(), saved_pass.c_str());

        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        unsigned long t = millis();
        // Wait up to OTA_WIFI_TIMEOUT_MS (15 seconds) to establish connection
        while (WiFi.status() != WL_CONNECTED && millis() - t < OTA_WIFI_TIMEOUT_MS) {
            delay(500);
            Serial.printf("[OTA] WiFi Status: %d\n", WiFi.status());
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            snprintf(buf, sizeof(buf), "WiFi connected, checking GitHub...");
            set_status(OTA_CHECKING_VERSION, 20, buf);
            Serial.printf("[OTA] Successfully connected to %s. IP: %s\n", saved_ssid.c_str(), WiFi.localIP().toString().c_str());
            prefs.end();
            return true;
        }
        
        Serial.printf("[OTA] Failed to connect to %s (Final Status: %d). Moving to next...\n", saved_ssid.c_str(), WiFi.status());
        
        // Failed to connect to this one — disconnect and try next
        WiFi.disconnect(false);
        delay(100);
    }
    prefs.end();

    Serial.println("[OTA] Exhausted all saved networks. Connection failed.");

    if (!found_any_saved) {
        set_status(OTA_FAILED_NO_WIFI, 0, "Failed: No WiFi network found (Add via QR settings)");
    } else {
        set_status(OTA_FAILED_NO_WIFI, 0, "Failed: Could not connect to saved networks");
    }
    return false;
}

// ── Version Check ─────────────────────────────────────────────────────────────
static bool fetch_version_info() {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();                 // skip cert validation (lite OTA)
    http.begin(client, OTA_VERSION_URL);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    int code = http.GET();
    if (code != 200) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Version check failed (HTTP %d)", code);
        set_status(OTA_FAILED_SERVER, 0, buf);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    char remote_ver[16] = {};
    char changelog[256]  = {};
    char fw_url[256]     = {};

    json_get_str(payload.c_str(), "version",      remote_ver, sizeof(remote_ver));
    json_get_str(payload.c_str(), "changelog",    changelog,  sizeof(changelog));
    json_get_str(payload.c_str(), "firmware_url", fw_url,     sizeof(fw_url));

    strncpy(s_status.available_version, remote_ver, sizeof(s_status.available_version) - 1);
    strncpy(s_status.changelog,         changelog,  sizeof(s_status.changelog) - 1);
    strncpy(s_pending_fw_url,           fw_url,     sizeof(s_pending_fw_url) - 1);

    // sd_files are re-fetched during install — no need to cache now

    if (version_is_newer(remote_ver, APP_VERSION)) {
        char buf[96];
        snprintf(buf, sizeof(buf), "v%s available! Tap Install.", remote_ver);
        set_status(OTA_UPDATE_AVAILABLE, 30, buf);
        return true;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Up to date (v%s)", APP_VERSION);
        set_status(OTA_UP_TO_DATE, 100, buf);
        return false;
    }
}

// ── Firmware Download & Flash ─────────────────────────────────────────────────
static bool download_and_flash_firmware(const char* url) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.begin(client, url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(60000);

    int code = http.GET();
    if (code != 200) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Firmware download failed (HTTP %d)", code);
        set_status(OTA_FAILED_SERVER, 0, buf);
        http.end();
        return false;
    }

    int total = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) {
        set_status(OTA_FAILED_FLASH, 0, "OTA: not enough partition space");
        http.end();
        return false;
    }

    uint8_t buf[1024];
    int written = 0;
    set_status(OTA_DOWNLOADING_FW, 0, "Downloading firmware...");

    while (http.connected() && (total == -1 || written < total)) {
        int avail = stream->available();
        if (avail) {
            int read = stream->readBytes(buf, min((int)sizeof(buf), avail));
            if (Update.write(buf, read) != (size_t)read) {
                set_status(OTA_FAILED_FLASH, 0, "Flash write error");
                http.end();
                return false;
            }
            written += read;
            if (total > 0) {
                char status_buf[64];
                int pct = (written * 100) / total;
                snprintf(status_buf, sizeof(status_buf), "Downloading firmware %d%%", pct);
                set_status(OTA_DOWNLOADING_FW, pct, status_buf);
            }
        }
        delay(1);
    }
    http.end();

    if (!Update.end(true)) {
        set_status(OTA_FAILED_FLASH, 0, "Firmware verification failed");
        return false;
    }
    Serial.println("[OTA] Firmware flashed successfully.");
    return true;
}

// ── SD Card File Updates ──────────────────────────────────────────────────────
static void download_sd_files() {
    // Re-fetch version.json to get the sd_files list
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.begin(client, OTA_VERSION_URL);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    String payload = http.getString();
    http.end();

    // Simple manual parser for sd_files array:
    // Looks for repeated {"path":"...","url":"..."} blocks
    const char* p = payload.c_str();
    const char* sd_files_start = strstr(p, "\"sd_files\"");
    if (!sd_files_start) return;

    int total_files = 0;
    // Count entries by counting occurrences of "path":" inside sd_files section
    const char* cnt = sd_files_start;
    while ((cnt = strstr(cnt, "\"path\":")) != NULL) { total_files++; cnt++; }
    if (total_files == 0) return;

    int done = 0;
    const char* cursor = sd_files_start;
    while (true) {
        // Find next path+url pair
        const char* path_pos = strstr(cursor, "\"path\":");
        if (!path_pos) break;
        const char* url_pos  = strstr(path_pos, "\"url\":");
        if (!url_pos) break;

        // Extract path value
        char sd_path[128] = {};
        const char* pv = strstr(path_pos, ":");
        if (pv) { pv++; while(*pv==' '||*pv=='"') pv++;
            size_t i=0; while(*pv && *pv!='"' && i<sizeof(sd_path)-1) sd_path[i++]=*pv++; }

        // Extract url value
        char dl_url[256] = {};
        const char* uv = strstr(url_pos, ":");
        if (uv) { uv++; while(*uv==' '||*uv=='"') uv++;
            // URL may contain : so read until closing quote
            size_t i=0; bool escaped=false;
            while(*uv && i<sizeof(dl_url)-1) {
                if (escaped) { dl_url[i++]=*uv++; escaped=false; }
                else if (*uv=='\\') { escaped=true; uv++; }
                else if (*uv=='"') break;
                else dl_url[i++]=*uv++;
            } }

        cursor = url_pos + 1;

        if (!sd_path[0] || !dl_url[0]) continue;

        char status_buf[96];
        snprintf(status_buf, sizeof(status_buf), "Updating SD files %d/%d", done + 1, total_files);
        set_status(OTA_DOWNLOADING_SD, (done * 100) / total_files, status_buf);

        HTTPClient fhttp;
        WiFiClientSecure fclient;
        fclient.setInsecure();
        fhttp.begin(fclient, dl_url);
        fhttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int fcode = fhttp.GET();
        if (fcode == 200) {
            String full_path = String("/sd_card") + sd_path;
            FILE* fp = fopen(full_path.c_str(), "wb");
            if (fp) {
                WiFiClient* fstream = fhttp.getStreamPtr();
                int fsize = fhttp.getSize();
                uint8_t fbuf[512];
                int fwritten = 0;
                while (fhttp.connected() && (fsize == -1 || fwritten < fsize)) {
                    int avail = fstream->available();
                    if (avail) {
                        int r = fstream->readBytes(fbuf, min((int)sizeof(fbuf), avail));
                        fwrite(fbuf, 1, r, fp);
                        fwritten += r;
                    }
                    delay(1);
                }
                fclose(fp);
                Serial.printf("[OTA] SD file updated: %s\n", sd_path);
            }
        }
        fhttp.end();
        done++;
    }
}

// ── Background Task ───────────────────────────────────────────────────────────
static void ota_task(void* param) {
    bool do_install = (param != NULL);

    if (do_install) {
        // User confirmed install — proceed with firmware download
        if (!download_and_flash_firmware(s_pending_fw_url)) {
            vTaskDelete(NULL);
            return;
        }
        download_sd_files();
        set_status(OTA_REBOOTING, 100, "Update complete! Rebooting...");
        delay(1500);
        ESP.restart();
    } else {
        // Just check version
        if (!connect_to_known_network()) {
            vTaskDelete(NULL);
            return;
        }
        fetch_version_info();
        // Disconnect STA — keep AP running normally
        WiFi.disconnect(false);
    }

    s_task_handle = NULL;
    vTaskDelete(NULL);
}

void ota_check_for_update() {
    if (s_task_handle != NULL) return; // already running
    s_install_requested = false;
    xTaskCreatePinnedToCore(ota_task, "OTA_check", 8192, NULL, 1, &s_task_handle, 0);
}

void ota_install() {
    if (s_status.state != OTA_UPDATE_AVAILABLE) return;
    if (s_task_handle != NULL) return;
    set_status(OTA_DOWNLOADING_FW, 0, "Starting download...");
    // Pass non-NULL to signal install mode
    xTaskCreatePinnedToCore(ota_task, "OTA_install", 8192, (void*)1, 1, &s_task_handle, 0);
}

const OTAStatus* ota_get_status() {
    return &s_status;
}

const char* ota_current_version() {
    return APP_VERSION;
}
