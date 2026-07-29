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
#include <sys/stat.h>    // mkdir() for creating SD subdirectories during sd_files sync

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

// ── SD-card persistence (survives firmware re-flash) ───────────────────────────
// WiFi networks are saved to the SD card as lines of "SSID<TAB>PASSWORD" so they
// outlive any firmware flash (NVS can be wiped by a full chip erase; the SD card
// is independent storage). NVS is still written as a backup.
#define OTA_WIFI_FILE "/sd_card/wifi.txt"

static int sd_load_networks(char ssids[][33], char passes[][65], int max_count) {
    FILE* f = fopen(OTA_WIFI_FILE, "r");
    if (!f) return 0;
    int n = 0;
    char line[160];
    while (n < max_count && fgets(line, sizeof(line), f)) {
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = '\0';
        if (line[0] == '\0') continue;
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        strncpy(ssids[n], line, 32);     ssids[n][32]  = '\0';
        strncpy(passes[n], tab + 1, 64); passes[n][64] = '\0';
        n++;
    }
    fclose(f);
    return n;
}

static bool sd_save_networks(char ssids[][33], char passes[][65], int count) {
    FILE* f = fopen(OTA_WIFI_FILE, "w");
    if (!f) { Serial.println("[OTA] WARN: could not write wifi.txt to SD"); return false; }
    for (int i = 0; i < count; i++) fprintf(f, "%s\t%s\n", ssids[i], passes[i]);
    fclose(f);
    return true;
}

// NVS was described as a "backup", but nothing ever read it back — every lookup
// went to SD alone. So with no card (or an unmounted one) the saved credentials
// were invisible to OTA, to the portal's network list, and to convoy, even
// though they were sitting in NVS the whole time. This is that fallback.
static int nvs_load_networks(char ssids[][33], char passes[][65], int max_count) {
    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NS, true)) return 0;
    int n = 0;
    for (int i = 0; i < OTA_MAX_NETWORKS && n < max_count; i++) {
        char ks[16], kp[16];
        snprintf(ks, sizeof(ks), "ssid_%d", i); snprintf(kp, sizeof(kp), "pass_%d", i);
        String s = prefs.getString(ks, ""); if (s == "") continue;
        String p = prefs.getString(kp, "");
        strncpy(ssids[n], s.c_str(), 32);  ssids[n][32]  = '\0';
        strncpy(passes[n], p.c_str(), 64); passes[n][64] = '\0';
        n++;
    }
    prefs.end();
    return n;
}

// SD first (survives a reflash), NVS second (survives a missing card).
static int load_networks(char ssids[][33], char passes[][65], int max_count) {
    int n = sd_load_networks(ssids, passes, max_count);
    if (n > 0) return n;
    n = nvs_load_networks(ssids, passes, max_count);
    if (n > 0) Serial.printf("[OTA] SD unavailable — using %d network(s) from NVS\n", n);
    return n;
}

// One-time migration: if the SD file is absent but NVS holds networks, copy them
// to SD so previously-saved credentials survive future flashes.
static void migrate_nvs_to_sd() {
    FILE* f = fopen(OTA_WIFI_FILE, "r");
    if (f) { fclose(f); return; } // SD file already present — nothing to migrate
    char ssids[OTA_MAX_NETWORKS][33]; char passes[OTA_MAX_NETWORKS][65];
    int n = nvs_load_networks(ssids, passes, OTA_MAX_NETWORKS);
    // Only claim success if the card actually took it. This used to log
    // "Migrated N network(s)" even when sd_save_networks() had just warned that
    // it could not write, which made a dead SD look like a working one.
    if (n > 0 && sd_save_networks(ssids, passes, n))
        Serial.printf("[OTA] Migrated %d network(s) NVS->SD\n", n);
}

// ── WiFi Network Storage ──────────────────────────────────────────────────────
void ota_init() {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_IDLE;
    strncpy(s_status.status_text, "Tap to check for updates", sizeof(s_status.status_text) - 1);
    migrate_nvs_to_sd();  // preserve any creds saved before the SD-persistence update
}

void ota_add_network(const char* ssid, const char* password) {
    migrate_nvs_to_sd();

    // --- SD (primary, survives reflash) ---
    char ssids[OTA_MAX_NETWORKS][33]; char passes[OTA_MAX_NETWORKS][65];
    int n = load_networks(ssids, passes, OTA_MAX_NETWORKS);
    int slot = -1;
    for (int i = 0; i < n; i++) if (strcmp(ssids[i], ssid) == 0) { slot = i; break; }
    if (slot < 0 && n < OTA_MAX_NETWORKS) slot = n++;
    if (slot >= 0) {
        strncpy(ssids[slot], ssid, 32);     ssids[slot][32]  = '\0';
        strncpy(passes[slot], password, 64); passes[slot][64] = '\0';
        sd_save_networks(ssids, passes, n);
        Serial.printf("[OTA] Saved network to SD: %s (slot %d)\n", ssid, slot);
    }

    // --- NVS (backup) ---
    Preferences prefs;
    prefs.begin(OTA_NVS_NS, false);
    for (int i = 0; i < OTA_MAX_NETWORKS; i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
        String existing = prefs.getString(key_ssid, "");
        if (existing == "" || existing == ssid) {
            prefs.putString(key_ssid, ssid);
            prefs.putString(key_pass, password);
            break;
        }
    }
    prefs.end();
}

void ota_remove_network(const char* ssid) {
    migrate_nvs_to_sd();

    // --- SD ---
    char ssids[OTA_MAX_NETWORKS][33]; char passes[OTA_MAX_NETWORKS][65];
    int n = load_networks(ssids, passes, OTA_MAX_NETWORKS);
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(ssids[i], ssid) != 0) {
            if (w != i) { strcpy(ssids[w], ssids[i]); strcpy(passes[w], passes[i]); }
            w++;
        }
    }
    sd_save_networks(ssids, passes, w);

    // --- NVS ---
    Preferences prefs;
    prefs.begin(OTA_NVS_NS, false);
    for (int i = 0; i < OTA_MAX_NETWORKS; i++) {
        char key_ssid[16], key_pass[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        snprintf(key_pass, sizeof(key_pass), "pass_%d", i);
        if (prefs.getString(key_ssid, "") == ssid) {
            prefs.remove(key_ssid);
            prefs.remove(key_pass);
            break;
        }
    }
    prefs.end();
    Serial.printf("[OTA] Removed network: %s\n", ssid);
}

int ota_list_networks(char ssids[][33], int max_count) {
    migrate_nvs_to_sd();
    char all_ssids[OTA_MAX_NETWORKS][33]; char all_passes[OTA_MAX_NETWORKS][65];
    int n = load_networks(all_ssids, all_passes, OTA_MAX_NETWORKS);
    int count = 0;
    for (int i = 0; i < n && count < max_count; i++) {
        strncpy(ssids[count], all_ssids[i], 32);
        ssids[count][32] = '\0';
        count++;
    }
    return count;
}

// ── WiFi Connect ──────────────────────────────────────────────────────────────
// report=false is the convoy caller: same radio sequence, but silent to the OTA
// UI and without the diagnostic scan (2-4s, log-only).
static bool wifi_connect_saved_core(bool report) {
    if (report) set_status(OTA_SCANNING_WIFI, 0, "Scanning for WiFi networks...");

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

    migrate_nvs_to_sd();
    char ssids[OTA_MAX_NETWORKS][33]; char passes[OTA_MAX_NETWORKS][65];
    int net_count = load_networks(ssids, passes, OTA_MAX_NETWORKS);
    bool found_any_saved = (net_count > 0);

    // --- Order the attempts by what is actually in range, strongest first ---
    // Each unreachable network costs a full OTA_WIFI_TIMEOUT_MS (15s) of dead
    // waiting, and on the road the home network is ALWAYS unreachable — convoy
    // was spending 15s staring at it before reaching the phone hotspot. The scan
    // costs a couple of seconds once and skips those timeouts entirely.
    int order[OTA_MAX_NETWORKS];
    int n_try = 0;
    int rssi[OTA_MAX_NETWORKS];
    const int n_scan = WiFi.scanNetworks(false, false);
    if (n_scan > 0) {
        for (int i = 0; i < net_count; i++) {
            rssi[i] = -1000;
            for (int j = 0; j < n_scan; j++)
                if (WiFi.SSID(j) == ssids[i] && WiFi.RSSI(j) > rssi[i]) rssi[i] = WiFi.RSSI(j);
            if (report)
                Serial.printf("[OTA] Saved '%s': %s\n", ssids[i],
                              rssi[i] > -1000 ? "in range" : "not seen");
        }
        for (int i = 0; i < net_count; i++) if (rssi[i] > -1000) order[n_try++] = i;
        for (int a = 1; a < n_try; a++) {          // insertion sort, strongest first
            int k = order[a], b = a - 1;
            while (b >= 0 && rssi[order[b]] < rssi[k]) { order[b + 1] = order[b]; b--; }
            order[b + 1] = k;
        }
    }
    if (n_try == 0) {
        // Scan failed, or none of the saved networks showed up. Fall back to
        // trying everything in stored order — a hidden SSID never appears in a
        // scan, so "not seen" is not proof that it is unreachable.
        Serial.printf("[OTA] Scan gave no usable order (scan=%d) — trying all %d saved\n",
                      n_scan, net_count);
        for (int i = 0; i < net_count; i++) order[n_try++] = i;
    }
    WiFi.scanDelete();

    Serial.printf("[OTA] Attempting %d saved network(s), best signal first...\n", n_try);

    for (int oi = 0; oi < n_try; oi++) {
        const int i = order[oi];
        const char* saved_ssid = ssids[i];
        const char* saved_pass = passes[i];

        if (report) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Connecting to %s...", saved_ssid);
            set_status(OTA_CONNECTING_WIFI, 10, buf);
        }

        Serial.printf("[OTA] Trying network %d: '%s'\n", i, saved_ssid);

        WiFi.begin(saved_ssid, saved_pass);
        unsigned long t = millis();
        // Wait up to OTA_WIFI_TIMEOUT_MS (15 seconds) to establish connection
        while (WiFi.status() != WL_CONNECTED && millis() - t < OTA_WIFI_TIMEOUT_MS) {
            delay(500);
            Serial.printf("[OTA] WiFi Status: %d\n", WiFi.status());
        }

        if (WiFi.status() == WL_CONNECTED) {
            if (report) set_status(OTA_CHECKING_VERSION, 20, "WiFi connected, checking GitHub...");
            Serial.printf("[OTA] Successfully connected to %s. IP: %s\n", saved_ssid, WiFi.localIP().toString().c_str());
            return true;
        }

        Serial.printf("[OTA] Failed to connect to %s (Final Status: %d). Moving to next...\n", saved_ssid, WiFi.status());

        // Failed to connect to this one — disconnect and try next
        WiFi.disconnect(false);
        delay(100);
    }

    Serial.println("[OTA] Exhausted all saved networks. Connection failed.");

    if (report) {
        if (!found_any_saved) {
            set_status(OTA_FAILED_NO_WIFI, 0, "Failed: No WiFi network found (Add via QR settings)");
        } else {
            set_status(OTA_FAILED_NO_WIFI, 0, "Failed: Could not connect to saved networks");
        }
    }
    return false;
}

static bool connect_to_known_network() { return wifi_connect_saved_core(true);  }
bool        ota_wifi_connect_saved()   { return wifi_connect_saved_core(false); }

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

        // Skip files already present on the SD card. This makes re-provisioning
        // cheap: a populated card downloads nothing; a blank card gets everything.
        {
            String existing = String("/sd_card") + sd_path;
            struct stat stt;
            if (stat(existing.c_str(), &stt) == 0 && stt.st_size > 0) { done++; continue; }
        }

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
            // Create any intermediate directories (fopen won't make them)
            {
                char tmp[160];
                strncpy(tmp, full_path.c_str(), sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = '\0';
                for (char* p = tmp + 1; *p; p++) {
                    if (*p == '/') { *p = '\0'; mkdir(tmp, 0777); *p = '/'; }
                }
            }
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
        // Normally WiFi is still connected from the version check (we no longer
        // disconnect after checking). If it dropped, reconnect — with one retry,
        // since phone hotspots are slow to re-admit a client right after a reset.
        if (WiFi.status() != WL_CONNECTED) {
            set_status(OTA_CONNECTING_WIFI, 5, "Reconnecting WiFi...");
            bool ok = connect_to_known_network();
            if (!ok) { delay(2500); ok = connect_to_known_network(); }
            if (!ok) { vTaskDelete(NULL); return; }
        }
        set_status(OTA_DOWNLOADING_FW, 0, "Downloading firmware...");
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
        // Just check version. Leave WiFi CONNECTED so a follow-up Install reuses
        // it — reconnecting to a phone hotspot right after a disconnect is flaky.
        if (!connect_to_known_network()) {
            vTaskDelete(NULL);
            return;
        }
        fetch_version_info();
        // If firmware is already current, still provision any MISSING SD files
        // (Option A: fresh/blank card gets its content; populated card is a no-op).
        if (s_status.state == OTA_UP_TO_DATE) {
            download_sd_files();
            set_status(OTA_UP_TO_DATE, 100, "Up to date");
        }
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
