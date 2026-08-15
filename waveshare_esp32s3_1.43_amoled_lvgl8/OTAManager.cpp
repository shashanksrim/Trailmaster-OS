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
#include <sys/stat.h>
#include <errno.h>    // mkdir() for creating SD subdirectories during sd_files sync

// ── Config ────────────────────────────────────────────────────────────────────
// This URL points to docs/version.json on your GitHub Pages site.
// Update this to match your actual GitHub username / repo name.
#define OTA_VERSION_URL  "https://raw.githubusercontent.com/shashanksrim/Trailmaster-OS/main/version.json"

// Photo-frame manifest, published by the convoy web app. Same shape as the
// version.json sd_files array: {"files":[{"path":"/photos/x.jpg","url":"..."}]}.
// Served from the repo neither — uploading a photo should not mean making a
// commit.
//
// Served by the relay Worker rather than the Realtime Database, and that is a
// safety property rather than a preference: this manifest decides what URLs the
// board downloads, and the database is world-writable, so anyone could have
// pointed it at any host. The Worker builds the list from the images it is
// actually holding, so every url in it is one the Worker serves.
// Keep in sync with RELAY_URL in docs/convoy/config.js.
#define OTA_PHOTO_MANIFEST_URL "https://trailmaster-relay.shashank-srim.workers.dev/photos.json"
#define SD_MOUNT_POINT   "/sd_card"
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

// MERGE both stores, SD first, de-duplicated by SSID.
//
// This used to be "SD if non-empty, else NVS", and that shadowing cost a real
// debugging session (2026-08-12): the SD file held ONE network whose password
// had gone stale, NVS held a second network that worked, and because SD was
// non-empty the working credential was never even tried. The board sat at
// "no saved network reachable" with a perfectly good AP in range.
//
// The two stores exist for different failure modes — SD survives a reflash, NVS
// survives a missing card — so neither is authoritative and picking one is
// always wrong. Trying both is strictly better: a credential that ever worked
// stays in the candidate pool, and the scan-ordered connect loop below only
// costs time on the ones actually in range.
static int load_networks(char ssids[][33], char passes[][65], int max_count) {
    int n = sd_load_networks(ssids, passes, max_count);
    const int from_sd = n;

    char nssids[OTA_MAX_NETWORKS][33], npasses[OTA_MAX_NETWORKS][65];
    int m = nvs_load_networks(nssids, npasses, OTA_MAX_NETWORKS);
    int added = 0;
    for (int i = 0; i < m && n < max_count; i++) {
        bool dup = false;
        for (int j = 0; j < from_sd; j++) if (strcmp(ssids[j], nssids[i]) == 0) { dup = true; break; }
        if (dup) continue;
        strncpy(ssids[n], nssids[i], 32);  ssids[n][32]  = '\0';
        strncpy(passes[n], npasses[i], 64); passes[n][64] = '\0';
        n++; added++;
    }
    Serial.printf("[OTA] credentials: %d from SD + %d new from NVS = %d candidate(s)\n",
                  from_sd, added, n);
    for (int i = 0; i < n; i++)
        Serial.printf("[OTA]   [%d] '%s' (pass %d chars)\n", i, ssids[i], (int)strlen(passes[i]));
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

// SSIDs *and* passwords, for the on-device "saved networks" page. Separate from
// ota_list_networks() because that one is used where only names are wanted, and
// handing passwords to callers that do not need them is how they end up in logs.
int ota_list_networks_full(char ssids[][33], char passes[][65], int max_count) {
    migrate_nvs_to_sd();
    char all_ssids[OTA_MAX_NETWORKS][33]; char all_passes[OTA_MAX_NETWORKS][65];
    int n = load_networks(all_ssids, all_passes, OTA_MAX_NETWORKS);
    int count = 0;
    for (int i = 0; i < n && count < max_count; i++) {
        strncpy(ssids[count],  all_ssids[i],  32); ssids[count][32]  = '\0';
        strncpy(passes[count], all_passes[i], 64); passes[count][64] = '\0';
        count++;
    }
    return count;
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
    // Dump the whole scan, always. "not seen" on its own cannot distinguish a
    // typo'd SSID from a 5 GHz-only AP from a genuinely absent one, and this is
    // the S3: it is 2.4 GHz only, so a dual-band router that a phone or laptop
    // joins happily may be invisible here. Seeing the actual airspace is the
    // difference between guessing and knowing.
    Serial.printf("[OTA] scan sees %d AP(s):\n", n_scan);
    for (int j = 0; j < n_scan; j++)
        Serial.printf("[OTA]   '%s' %d dBm ch%d\n",
                      WiFi.SSID(j).c_str(), (int)WiFi.RSSI(j), (int)WiFi.channel(j));
    if (n_scan > 0) {
        for (int i = 0; i < net_count; i++) {
            rssi[i] = -1000;
            for (int j = 0; j < n_scan; j++)
                if (WiFi.SSID(j) == ssids[i] && WiFi.RSSI(j) > rssi[i]) rssi[i] = WiFi.RSSI(j);
            Serial.printf("[OTA] saved '%s': %s\n", ssids[i],
                          rssi[i] > -1000 ? "IN RANGE" : "not seen (2.4 GHz only on this chip)");
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

        // Name the failure. wl_status_t on its own sends you hunting through
        // headers mid-debug, and the two common causes need different fixes:
        // NO_SSID_AVAIL is "out of range / 5 GHz-only / hidden", while
        // CONNECT_FAILED and DISCONNECTED after a full timeout are almost always
        // a wrong password.
        {
            const int st = (int)WiFi.status();
            const char *why =
                st == WL_NO_SSID_AVAIL  ? "SSID not found (out of range, hidden, or 5 GHz-only — the S3 is 2.4 GHz only)" :
                st == WL_CONNECT_FAILED ? "auth rejected — wrong password?" :
                st == WL_DISCONNECTED   ? "no association within the timeout — usually a wrong password" :
                                          "unknown";
            Serial.printf("[OTA] FAILED '%s' (status %d: %s)\n", saved_ssid, st, why);
        }

        // Failed to connect to this one — disconnect and try next
        WiFi.disconnect(false);
        delay(100);
    }

    Serial.printf("[OTA] Exhausted all %d saved network(s) — still offline. "
                  "Re-enter the password via the setup portal if the AP is in range.\n", n_try);

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
// Download every {"path":..,"url":..} pair found under `key` onto the SD card,
// skipping anything already present.
//
// Shared by two callers with the same shape and the same skip-if-present
// behaviour: the OTA asset sync (`sd_files` in version.json) and photo sync
// (`files` in the photo manifest). Photos used to require the phone to join the
// board's access point and push them over the local network; pulling them from a
// URL instead means photo setup can live at the same public URL as everything
// else, and the captive portal is left doing only the Wi-Fi bootstrap that
// genuinely cannot happen anywhere else.
// Join the SD root to a manifest path with exactly one slash between them.
//
// The two manifests disagree on shape and always have: version.json's sd_files
// carry a leading slash ("/photos/x.bin"), while the photo Worker names files
// bare ("tm_abc.bin") because they live in a flat KV keyspace. Concatenating
// blindly produced "/sd_cardtm_abc.bin" — a path outside the card, so the
// download reported success and the frame stayed empty.
static String sd_full_path(const char* sd_path) {
    String p(sd_path);
    if (!p.startsWith("/")) p = "/" + p;
    return String(SD_MOUNT_POINT) + p;
}

static int download_file_list(const String& payload, const char* key, const char* label) {
    const char* p = payload.c_str();
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* sd_files_start = strstr(p, pat);
    if (!sd_files_start) return 0;

    int total_files = 0;
    // Count entries by counting occurrences of "path":" inside sd_files section
    const char* cnt = sd_files_start;
    while ((cnt = strstr(cnt, "\"path\":")) != NULL) { total_files++; cnt++; }
    if (total_files == 0) return 0;

    int done = 0, failed = 0;
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
            String existing = sd_full_path(sd_path);
            struct stat stt;
            if (stat(existing.c_str(), &stt) == 0 && stt.st_size > 0) { done++; continue; }
        }

        char status_buf[96];
        snprintf(status_buf, sizeof(status_buf), "%s %d/%d", label, done + 1, total_files);
        set_status(OTA_DOWNLOADING_SD, (done * 100) / total_files, status_buf);

        HTTPClient fhttp;
        WiFiClientSecure fclient;
        fclient.setInsecure();
        fhttp.begin(fclient, dl_url);
        fhttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int fcode = fhttp.GET();
        if (fcode == 200) {
            String full_path = sd_full_path(sd_path);
            Serial.printf("[OTA]   '%s' -> %s\n", sd_path, full_path.c_str());
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
                Serial.printf("[OTA] %s: wrote %s (%d bytes)\n", label, sd_path, fwritten);
            } else {
                // Silent before, which is how a whole sync could report success
                // while writing nothing: the only clue was an empty frame.
                Serial.printf("[OTA] %s: CANNOT OPEN %s (errno %d)\n",
                              label, full_path.c_str(), errno);
                failed++;
            }
        } else {
            Serial.printf("[OTA] %s: GET %s -> %d\n", label, dl_url, fcode);
            failed++;
        }
        fhttp.end();
        done++;
    }
    return failed ? -1 : done;
}

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
    download_file_list(payload, "sd_files", "Updating SD files");
}

// Pull photo-frame images from the manifest the web app publishes.
//
// Deliberately runs on the OTA connection path rather than acquiring the radio
// itself: that path already picks the strongest saved network, which at home is
// home Wi-Fi. Syncing over the phone's hotspot would push every photo up to the
// cloud and straight back down through the same cellular link — paying for the
// bytes twice — so preferring the network the board is already best connected to
// is the whole point.
void ota_sync_photos() {
    ota_photos_busy = true;
    if (WiFi.status() != WL_CONNECTED) {
        set_status(OTA_CONNECTING_WIFI, 0, "Connecting to Wi-Fi...");
        if (!ota_wifi_connect_saved()) {
            set_status(OTA_IDLE, 0, "No Wi-Fi in range");
            Serial.println("[OTA] photo sync: no network");
            ota_photos_busy = false;
            return;
        }
    }
    set_status(OTA_CONNECTING_WIFI, 0, "Checking for images...");
    // Ask only for THIS board's images. The id is the eFuse MAC, derived
    // identically to convoy_wifi.h's devices/<id>, which is how the app knows
    // what to upload under. Without the scope every Trailmaster on a trip would
    // download every image any of them was ever sent.
    const uint64_t mac = ESP.getEfuseMac();
    char manifest_url[192];
    snprintf(manifest_url, sizeof(manifest_url), "%s?dev=%04X%08X",
             OTA_PHOTO_MANIFEST_URL, (uint16_t)(mac >> 32), (uint32_t)mac);

    // SCOPED so the TLS client is DESTROYED before any file download starts.
    //
    // Each WiFiClientSecure holds ~40 KB of handshake buffers in internal RAM,
    // and download_file_list opens its own. Keeping this one alive meant two at
    // once, which the heap cannot take: the manifest fetch succeeded and every
    // file GET to the same host then failed with -1, while the sync still
    // reported "up to date". http.end() is not enough — it closes the request,
    // not the client's buffers.
    String payload;
    {
        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure();
        http.begin(client, manifest_url);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int code = http.GET();
        if (code != 200) {
            Serial.printf("[OTA] photo manifest -> %d\n", code);
            set_status(OTA_IDLE, 0, "Could not reach the image store");
            http.end(); ota_photos_busy = false; return;
        }
        payload = http.getString();
        http.end();
        client.stop();
    }
    Serial.printf("[OTA] internal RAM before downloads: %u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    // An empty manifest reads as the literal "null" from the Realtime Database.
    if (payload.length() < 4) {
        Serial.println("[OTA] photo manifest empty");
        set_status(OTA_IDLE, 0, "No images in the app yet");
        ota_photos_busy = false; return;
    }
    Serial.printf("[OTA] photo manifest: %s\n", payload.c_str());
    const int got = download_file_list(payload, "files", "Syncing images");
    // Report what actually happened. "Up to date" after two failed downloads is
    // how this hid for three rounds of debugging.
    char done_msg[64];
    if (got < 0) snprintf(done_msg, sizeof(done_msg), "Some images failed to download");
    else         snprintf(done_msg, sizeof(done_msg), "Images up to date");
    set_status(OTA_IDLE, 0, done_msg);
    ota_photos_busy = false;
}

// Run the photo sync off the UI thread. It makes HTTPS requests and writes
// several hundred KB to the SD card per image, so calling it inline would freeze
// LVGL for seconds — and it is triggered from a screen the user is looking at.
//
// Its own task rather than ota_task's: that one owns the firmware update state
// machine and announces itself through the OTA overlay, which would make a photo
// sync look like a pending firmware update.
static TaskHandle_t s_photo_task = NULL;
volatile bool ota_photos_busy = false;   // a sync is running; the frame shows a toast
volatile bool ota_photos_changed = false;   // set when new files landed; UI clears it

static void ota_photo_task(void *) {
    const int before = 0;
    (void)before;
    ota_sync_photos();
    ota_photos_changed = true;              // the frame rescans and rebuilds
    s_photo_task = NULL;
    vTaskDelete(NULL);
}

void ota_sync_photos_async() {
    if (s_photo_task) return;               // already running
    xTaskCreatePinnedToCore(ota_photo_task, "photo_sync", 8192, NULL, 1, &s_photo_task, 0);
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
