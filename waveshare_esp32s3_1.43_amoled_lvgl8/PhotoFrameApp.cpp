#include "PhotoFrameApp.h"
#include <vector>
#include <algorithm>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <dirent.h>
#include <sys/stat.h>
#include "sd_card_bsp.h"
#include "amoled.h"
#include "esp_cache.h"
#include <AnimatedGIF.h>
#include "OTAManager.h"
#include "pair_code.h"   // the six characters a phone types to pair with THIS board

extern Amoled amoled;

// --- STABLE FUNCTIONAL HTML FRONTEND ---
const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Trailmaster Sync</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
    <style>
        :root { --primary: #e67e22; --bg: #121212; --card: #1e1e1e; --text: #f5f5f5; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; overscroll-behavior-y: contain; }
        .header { background: #000; width: 100%; padding: 20px 0; text-align: center; border-bottom: 3px solid var(--primary); }
        .header h1 { margin: 0; font-size: 24px; letter-spacing: 2px; text-transform: uppercase; display: flex; align-items: center; justify-content: center; gap: 10px; }
        .container { padding: 20px; width: 100%; max-width: 400px; box-sizing: border-box; }
        .card { background: var(--card); padding: 25px; border-radius: 12px; box-shadow: 0 8px 16px rgba(0,0,0,0.5); text-align: center; }
        .warning-banner { background: rgba(230, 126, 34, 0.1); border: 1px solid var(--primary); color: #e67e22; padding: 10px; border-radius: 8px; font-size: 13px; margin-bottom: 20px; text-align: left; line-height: 1.4; }
        .upload-btn-wrapper { position: relative; overflow: hidden; display: inline-block; width: 100%; margin-bottom: 15px; }
        .btn-outline { border: 2px dashed #555; color: #aaa; background: transparent; padding: 20px; border-radius: 8px; width: 100%; font-size: 16px; cursor: pointer; transition: 0.3s; }
        .btn-outline:hover { border-color: var(--primary); color: var(--primary); }
        .upload-btn-wrapper input[type=file] { font-size: 100px; position: absolute; left: 0; top: 0; opacity: 0; cursor: pointer; height: 100%; }
        .btn-primary { background: var(--primary); color: white; border: none; padding: 15px; font-size: 16px; border-radius: 8px; cursor: pointer; width: 100%; font-weight: bold; text-transform: uppercase; transition: 0.3s; margin-top: 15px; }
        .btn-primary:hover { background: #d35400; }
        .btn-primary:disabled { background: #555; color: #888; cursor: not-allowed; }
        #editorControls { display: none; width: 100%; }
        .preview-container { margin: 0 auto 15px auto; width: 240px; height: 240px; border-radius: 50%; overflow: hidden; border: 4px solid #333; box-shadow: 0 0 20px rgba(0,0,0,0.8); position: relative; background: #000; touch-action: none; }
        canvas { width: 100%; height: 100%; cursor: grab; }
        canvas:active { cursor: grabbing; }
        .slider-container { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; }
        .slider-container input[type=range] { flex-grow: 1; accent-color: var(--primary); }
        .hint { font-size: 12px; color: #888; margin-bottom: 15px; }
        .settings { margin-top: 20px; text-align: left; background: #252525; padding: 15px; border-radius: 8px; font-size: 14px; }
        .settings summary { cursor: pointer; font-weight: bold; color: #aaa; outline: none; }
        .settings label { display: block; margin-top: 10px; cursor: pointer; color: #ccc; }
        #status { margin-top: 15px; font-weight: bold; min-height: 20px; color: var(--primary); }
        .back { display: inline-block; margin-bottom: 16px; color: #888; text-decoration: none; font-size: 13px; }
    </style>
</head>
<body>
    <div class="header">
        <h1>
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="var(--primary)" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"></path>
                <circle cx="12" cy="10" r="3"></circle>
            </svg>
            TRAILMASTER
        </h1>
    </div>
    <div class="container">
        <a class="back" href="/wifi">&larr; Wi-Fi setup</a>
        <div class="card">
            <div class="warning-banner" id="macWarning">
                <strong>Notice:</strong> If the sync is not happening then refresh this page or toggle wifi on/off & select it again from your source device wifi menu.
            </div>
            <p style="color: #aaa; margin-top: 0; margin-bottom: 20px;">Sync device backgrounds</p>
            <div id="editorControls">
                <div class="preview-container" id="previewContainer">
                    <canvas id="canvas" width="466" height="466"></canvas>
                </div>
                <div class="hint">Drag image to pan</div>
                <div class="slider-container">
                    <span style="font-size: 18px;">-</span>
                    <input type="range" id="zoomSlider" min="1" max="3" step="0.01" value="1">
                    <span style="font-size: 18px;">+</span>
                </div>
            </div>
            <div style="display: flex; gap: 10px; width: 100%; align-items: stretch;">
                <div class="upload-btn-wrapper" style="flex: 1; margin: 0;">
                    <button class="btn-outline" id="selectBtn" style="padding: 15px; width: 100%; height: 100%; font-size: 14px; box-sizing: border-box;">Select Photo</button>
                    <input type="file" id="fileInput" accept="image/*" onchange="loadImage()">
                </div>
                <button class="btn-primary" id="uploadBtn" onclick="upload()" style="flex: 1; margin: 0; padding: 15px; font-size: 14px; box-sizing: border-box;" disabled>Send to Device</button>
            </div>
            <div id="status"></div>

        </div>
    </div>
    <script>
        let img = new Image();
        let canvas = document.getElementById('canvas');
        let ctx = canvas.getContext('2d');
        let zoomSlider = document.getElementById('zoomSlider');
        let scale = 1, minScale = 1;
        let offsetX = 0, offsetY = 0;
        let isDragging = false;
        let startX, startY;
        let imageReady = false;

        async function loadImage() {
            const file = document.getElementById('fileInput').files[0];
            if (!file) return;
            document.getElementById('selectBtn').innerText = "Change Photo";
            img.src = URL.createObjectURL(file);
            await new Promise(r => img.onload = r);
            minScale = Math.max(466 / img.width, 466 / img.height);
            scale = minScale;
            offsetX = (466 - img.width * scale) / 2;
            offsetY = (466 - img.height * scale) / 2;
            zoomSlider.min = minScale;
            zoomSlider.max = minScale * 3;
            zoomSlider.value = scale;
            document.getElementById('editorControls').style.display = 'block';
            document.getElementById('uploadBtn').disabled = false;
            imageReady = true;
            draw();
        }

        function draw() {
            if (!imageReady) return;
            const maxOffsetX = 0;
            const minOffsetX = 466 - (img.width * scale);
            const maxOffsetY = 0;
            const minOffsetY = 466 - (img.height * scale);
            if (offsetX > maxOffsetX) offsetX = maxOffsetX;
            if (offsetX < minOffsetX) offsetX = minOffsetX;
            if (offsetY > maxOffsetY) offsetY = maxOffsetY;
            if (offsetY < minOffsetY) offsetY = minOffsetY;
            ctx.fillStyle = 'black';
            ctx.fillRect(0, 0, 466, 466);
            ctx.drawImage(img, offsetX, offsetY, img.width * scale, img.height * scale);
        }

        function getPos(e) {
            if(e.touches) return {x: e.touches[0].clientX, y: e.touches[0].clientY};
            return {x: e.clientX, y: e.clientY};
        }

        canvas.onmousedown = canvas.ontouchstart = (e) => {
            e.preventDefault();
            isDragging = true;
            let pos = getPos(e);
            startX = pos.x;
            startY = pos.y;
        };

        window.onmousemove = window.ontouchmove = (e) => {
            if (!isDragging) return;
            e.preventDefault();
            let pos = getPos(e);
            let rect = canvas.getBoundingClientRect();
            let scaleFactor = 466 / rect.width;
            offsetX += (pos.x - startX) * scaleFactor;
            offsetY += (pos.y - startY) * scaleFactor;
            startX = pos.x;
            startY = pos.y;
            draw();
        };

        window.onmouseup = window.ontouchend = () => { isDragging = false; };

        zoomSlider.oninput = (e) => {
            let oldScale = scale;
            scale = parseFloat(e.target.value);
            let centerX = 466 / 2;
            let centerY = 466 / 2;
            offsetX = centerX - (centerX - offsetX) * (scale / oldScale);
            offsetY = centerY - (centerY - offsetY) * (scale / oldScale);
            draw();
        };

        async function upload() {
            if (!imageReady) return;
            document.getElementById('status').innerText = 'Processing image...';
            document.getElementById('uploadBtn').disabled = true;
            const imgData = ctx.getImageData(0, 0, 466, 466).data;
            const rgb565Data = new Uint8Array(466 * 466 * 2);
            let j = 0;
            for (let i = 0; i < imgData.length; i += 4) {
                let r = imgData[i];
                let g = imgData[i+1];
                let b = imgData[i+2];
                const rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                rgb565Data[j++] = rgb565 & 0xFF;
                rgb565Data[j++] = (rgb565 >> 8) & 0xFF;
            }
            document.getElementById('status').innerText = 'Uploading to Device...';
            const blob = new Blob([rgb565Data], {type: 'application/octet-stream'});
            const filename = "img_" + Date.now() + ".bin";
            const formData = new FormData();
            formData.append('image', blob, filename);
            try {
                const res = await fetch('/upload', { method: 'POST', body: formData });
                if (res.ok) {
                    document.getElementById('status').innerText = 'Sync Complete!';
                    document.getElementById('status').style.color = '#4CAF50';
                } else {
                    document.getElementById('status').innerText = 'Upload Failed.';
                    document.getElementById('status').style.color = '#f44336';
                }
            } catch (e) {
                document.getElementById('status').innerText = 'Error: ' + e;
                document.getElementById('status').style.color = '#f44336';
            }
            document.getElementById('uploadBtn').disabled = false;
        }
    </script>
</body>
</html>
)rawliteral";

// --- Cloud handoff ---------------------------------------------------------
// The board portal only provisions Wi-Fi; convoy, settings and photos live in
// the cloud PWA. Once credentials are saved there is nothing left for the phone
// to do on the AP, so the save page hands the user straight over.
const char* CLOUD_URL = "tinyurl.com/trailmstr";
// SSIDs are attacker-chosen strings that get reflected into the portal's HTML,
// so they go through here on the way in. Cheap insurance; an SSID containing a
// quote would otherwise break the Remove form even without any malice.
static String html_escape(const char* s) {
    String out;
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += *p;       break;
        }
    }
    return out;
}

static String url_encode(const String& s) {
    const char* hex = "0123456789ABCDEF";
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%'; out += hex[(c >> 4) & 0xF]; out += hex[c & 0xF];
        }
    }
    return out;
}

// --- Photo Frame Globals ---
const char* AP_SSID = "Jimny_Dash_Sync";
const char* AP_PASSWORD = "password123";
const char* SD_ROOT = "/sd_card";
WebServer photo_server(80);
DNSServer photo_dnsServer;
const byte DNS_PORT = 53;
FILE* uploadFile = NULL;
std::vector<String> image_files;
int current_image_index = 0;
volatile bool new_image_uploaded = false;
unsigned long last_image_change = 0;
const unsigned long SLIDESHOW_INTERVAL = 3000;
bool delete_dialog_open = false;
bool slideshow_active = false; 

lv_obj_t * photoframe_screen = NULL;
lv_obj_t * pf_pages         = NULL;  // images + upload page (last)
lv_obj_t * pf_dot_cont      = NULL;  // pagination dot bar

#define PF_CACHE_SIZE 3
uint8_t * pf_buffers[PF_CACHE_SIZE] = {NULL};
lv_img_dsc_t pf_dscs[PF_CACHE_SIZE];
// Slot 0/1/2 = previous / current / next image around the visible page.
int pf_buffer_loaded_index[PF_CACHE_SIZE] = {-1, -1, -1};
static uint32_t last_scroll_time = 0;
static int pf_pending_scroll_idx = -1;
static bool pf_carousel_dirty = false;
static lv_obj_t * pf_upload_overlay = NULL;
static bool upload_overlay_open = false;
static uint32_t upload_overlay_opened_ms = 0;

bool wifi_ap_running = false;
// Which flavour of the overlay is showing — see pf_show_upload_overlay().
static bool pf_overlay_wifi_only = false;
bool pf_autostart_wifi = false;  // when true, the WiFi overlay opens with the hotspot ON
static uint32_t menu_opened_time = 0;

// Deadline for the post-provisioning handoff, 0 when none is pending. Saving a
// network arms it; the loop then swaps the on-screen QR from "join my hotspot"
// to "open the app" and drops the AP.
//
// Deferred rather than done in the request handler for two reasons: tearing the
// AP down there would close the server out from under its own response, and
// LVGL work has to happen on the UI thread. The delay also lets the success
// page render and be read before the network under it disappears.
static volatile uint32_t pf_provisioned_at = 0;

extern void switch_to_launcher();

// --- Header Functions ---
void scan_images();
void show_image(int index);
void next_image();
void prev_image();
void delete_current_image();
void start_photoframe_wifi();
void stop_photoframe_wifi();
static void rebuild_pf_pages();
static int pf_total_pages(void);
static int pf_upload_page_idx(void);
static bool pf_is_upload_page(int page_idx);
static void pf_build_upload_gateway_page(lv_obj_t * parent);
void pf_show_upload_overlay(bool wifi_only);
static void pf_close_upload_overlay(void);
static void pf_navigate_to_page(int page_idx);
static void pf_on_page_settled(int page_idx);
static void pf_apply_carousel_refresh_after_upload(void);
static void pf_load_page_immediately(int idx, bool load_neighbors);
static void pf_invalidate_full_screen(void);
static void pf_update_dots(int page_idx);
static void pf_style_page_img(lv_obj_t * img);
static bool pf_cache_load_slot(int slot, int img_idx);
static void pf_bind_img_idx_to_pages(int img_idx, int slot);

// Horizontal scroll only dirtys a strip of the display; force a full-screen LVGL pass.
static void pf_invalidate_full_screen(void) {
    if (lv_scr_act() != photoframe_screen) return;
    lv_disp_t * disp = lv_disp_get_default();
    if (disp && disp->driver) {
        disp->driver->full_refresh = 1;
    }
    if (photoframe_screen) {
        lv_obj_invalidate(photoframe_screen);
    }
    lv_refr_now(disp);
    if (disp && disp->driver) {
        disp->driver->full_refresh = 0;
    }
}


// Last carousel page: one button — tap opens the full-screen upload overlay (QR + Wi-Fi).
static void pf_build_upload_gateway_page(lv_obj_t * parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 300, 64);
    lv_obj_center(btn);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x606060), 0); // Steel grey button
    lv_obj_set_style_radius(btn, 32, 0);
    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Upload images");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        pf_show_upload_overlay(false);   // photo upload gateway: keeps the toggle
    }, LV_EVENT_CLICKED, NULL);
}

// wifi_only = opened for Wi-Fi setup (settings, first run) rather than to
// upload photos. Those two want genuinely different screens:
//
//   wifi_only  no toggle, hotspot starts by itself, large QR
//   photos     "Enable Wi-Fi" toggle, small QR
//
// The toggle exists because auto-starting the AP left the display garbled after
// the overlay closed. Investigation (WIFI_TOGGLE_INVESTIGATION.md) found that
// was never a radio/LVGL collision: it was a DROPPED FLUSH, a full 466x466 frame
// against a 4092-byte max_transfer_sz, and it is fixed in amoled.cpp by banding.
//
// It is dropped only for wifi_only because that case is structurally immune
// regardless: the full-frame redraw comes from pf_invalidate_full_screen(),
// which early-returns unless the PHOTO screen is active. From settings or first
// run it never runs, so the failure mode cannot occur. The photo path keeps the
// toggle until Firebase photo sync retires AP uploads altogether.
// ── Wi-Fi settings menu ─────────────────────────────────────────────────────
// "Wifi settings" used to drop you straight onto the QR code. That is the right
// destination when you are ADDING a network, but it is the wrong one when the
// question is "what does this thing already know?" — and after a failed drive
// that is the more common question. Debugging a non-connecting board on
// 2026-08-12 meant a USB serial capture purely to read back the stored SSIDs,
// which is a thing the device already knows and should simply show.
//
// So: a two-item menu. Saved networks first (inspect), configure second (change).
static lv_obj_t *pf_wifi_menu = NULL;

static void pf_close_wifi_menu(lv_event_t *e) {
    (void)e;
    if (pf_wifi_menu) { lv_obj_del(pf_wifi_menu); pf_wifi_menu = NULL; }
}

// Full-screen sheet with the app's standard X, used by both pages below.
static lv_obj_t *pf_wifi_sheet(const char *title) {
    lv_obj_t *ov = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ov, 466, 466);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(ov, 255, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ov);

    lv_obj_t *x = lv_btn_create(ov);
    lv_obj_set_size(x, 60, 60);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(0x333333), 0);
    lv_obj_align(x, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_ext_click_area(x, 36);
    lv_obj_t *lx = lv_label_create(x);
    lv_label_set_text(lx, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lx, &lv_font_montserrat_20, 0);
    lv_obj_center(lx);
    lv_obj_add_event_cb(x, pf_close_wifi_menu, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = lv_label_create(ov);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFF6A00), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 92);
    return ov;
}

// A settings-style row (400x80 elsewhere; 356 here so the corners clear the
// round bezel, same trim as the convoy order list).
// Two looks, and the difference is a promise about behaviour: a CARD is raised
// and outlined because tapping it does something, a PLAIN row is just text on
// the sheet because it does not. The saved-SSID list used cards and so invited
// taps that were never wired to anything.
typedef enum { PF_ROW_CARD, PF_ROW_PLAIN, PF_ROW_DANGER } pf_row_style_t;

static lv_obj_t *pf_wifi_row_ex(lv_obj_t *parent, const char *title, const char *sub,
                                pf_row_style_t style) {
    const bool two_line = sub && strchr(sub, '\n') != NULL;
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 356, style == PF_ROW_PLAIN ? 64 : (two_line ? 96 : 80));
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 12, 0);

    uint32_t title_col = 0xFFFFFF;
    if (style == PF_ROW_PLAIN) {
        // Nothing at all — no fill, no outline, no rule between entries. The
        // spacing carries the grouping, and dropping the separators is what
        // stops a read-only list from reading as a stack of controls.
        lv_obj_set_style_bg_opa(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
    } else if (style == PF_ROW_DANGER) {
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A1512), 0);
        lv_obj_set_style_bg_opa(row, 255, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x7A2E28), 0);
        title_col = 0xFF6B5E;
    } else {
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(row, 255, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x333333), 0);
    }

    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(title_col), 0);
    int t_dy = 0;
    if (sub && sub[0]) t_dy = (style == PF_ROW_PLAIN) ? -11 : (two_line ? -24 : -13);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, t_dy);

    if (sub && sub[0]) {
        lv_obj_t *sl = lv_label_create(row);
        lv_label_set_text(sl, sub);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sl, lv_color_hex(style == PF_ROW_DANGER ? 0xB07068 : 0x888888), 0);
        lv_obj_set_style_text_line_space(sl, 2, 0);
        lv_obj_align(sl, LV_ALIGN_LEFT_MID, 6, style == PF_ROW_PLAIN ? 12 : (two_line ? 12 : 14));
    }
    return row;
}

static lv_obj_t *pf_wifi_row(lv_obj_t *parent, const char *title, const char *sub) {
    return pf_wifi_row_ex(parent, title, sub, PF_ROW_CARD);
}

static void pf_forget_all_cb(lv_event_t *e);
static uint32_t pf_forget_armed_ms = 0;   // first tap of the two-tap confirm

// Page 2: what the board actually has stored, passwords included. Showing them
// in clear is deliberate — this is a private dash, and a settings page that hid
// the one field you need to verify would just send you back to a serial cable.
static void pf_show_saved_networks(lv_event_t *e) {
    (void)e;
    pf_forget_armed_ms = 0;        // never inherit an arm from a previous visit
    pf_close_wifi_menu(NULL);
    lv_obj_t *ov = pf_wifi_sheet("NETWORKS");
    pf_wifi_menu = ov;

    lv_obj_t *box = lv_obj_create(ov);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 376, 272);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);

    char ssids[8][33], passes[8][65];
    int n = ota_list_networks_full(ssids, passes, 8);
    if (n == 0) {
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *m = lv_label_create(box);
        lv_label_set_text(m, "No networks saved.");
        lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(m, lv_color_hex(0x8CA6B6), 0);
        lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    // The action goes ABOVE the list. It is the only control on this screen, and
    // putting it under a list that scrolls would hide it exactly when there are
    // most networks to clear.
    //
    // Forgetting is also the ONLY way back to the unprovisioned state: once the
    // board has a network the hotspot stops starting, so the web portal — where
    // the per-network Remove buttons live — cannot be reached at all.
    lv_obj_t *forget = pf_wifi_row_ex(box, "Forget all networks", "Returns to Wi-Fi setup",
                                      PF_ROW_DANGER);
    lv_obj_add_flag(forget, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(forget, pf_forget_all_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < n; i++) pf_wifi_row_ex(box, ssids[i], passes[i], PF_ROW_PLAIN);
}

static void pf_forget_all_cb(lv_event_t *e) {
    lv_obj_t *row   = lv_event_get_target(e);
    lv_obj_t *title = lv_obj_get_child(row, 0);
    lv_obj_t *sub   = lv_obj_get_child(row, 1);

    // Two taps, because this sits one tap away from a screen people open just to
    // read a password back, and the only recovery from a stray tap is walking
    // the whole provisioning flow again.
    if (!pf_forget_armed_ms || lv_tick_elaps(pf_forget_armed_ms) > 4000) {
        pf_forget_armed_ms = lv_tick_get();
        if (title) {
            lv_label_set_text(title, "Tap again to confirm");
            lv_obj_set_style_text_color(title, lv_color_hex(0xFF5A4F), 0);
        }
        if (sub) lv_label_set_text(sub, "Erases every saved network");
        return;
    }
    pf_forget_armed_ms = 0;

    char ssids[8][33] = {};
    int n = ota_list_networks(ssids, 8);
    for (int i = 0; i < n; i++) ota_remove_network(ssids[i]);   // clears SD and NVS
    Serial.printf("[PF] forgot %d saved network(s) — back to Wi-Fi setup\n", n);

    // Straight back to setup rather than to an empty list. Erasing the last
    // network puts the board in the unprovisioned state, and the QR is the whole
    // of what that state offers — the same screen a first-run board shows.
    //
    // Deferred: this callback is running on a row inside the sheet being torn
    // down, and deleting that from its own event handler is how you get a
    // use-after-free out of LVGL.
    lv_async_call([](void *) {
        pf_close_wifi_menu(NULL);
        pf_show_upload_overlay(true);
    }, NULL);
}

static void pf_open_wifi_qr(lv_event_t *e) {
    (void)e;
    pf_close_wifi_menu(NULL);
    pf_show_upload_overlay(true);
}

// With nothing saved there is no list worth showing — the only useful thing is
// the setup QR, so go straight to it rather than making the user read an empty
// screen and find the other row. Once networks exist this opens the list, which
// is also where Forget lives.
static void pf_open_networks(lv_event_t *e) {
    char ssids[8][33] = {};
    if (ota_list_networks(ssids, 8) == 0) { pf_open_wifi_qr(e); return; }
    pf_show_saved_networks(e);
}

void pf_show_wifi_menu(void) {
    if (pf_wifi_menu) return;

    // With no network saved there is nothing behind either menu row worth
    // choosing between: the companion app is unreachable and the only list is
    // empty. Skip the menu entirely and open setup. The menu appears from the
    // next visit on, once there is something to manage.
    {
        char probe[8][33] = {};
        if (ota_list_networks(probe, 8) == 0) { pf_show_upload_overlay(true); return; }
    }

    lv_obj_t *ov = pf_wifi_sheet("COMPANION & WI-FI");
    pf_wifi_menu = ov;

    lv_obj_t *box = lv_obj_create(ov);
    lv_obj_remove_style_all(box);
    // Three rows now (80 + 96 + 64 + gaps), and it starts higher to fit them.
    // The last row is PLAIN, so its box overrunning the round bezel costs
    // nothing visually — there is no fill or outline to be clipped, only
    // left-aligned text that stays well inside.
    lv_obj_set_size(box, 376, 272);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 124);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *r1 = pf_wifi_row(box, "Networks", "Configure SSIDs and passwords");
    lv_obj_add_flag(r1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r1, pf_open_networks, LV_EVENT_CLICKED, NULL);

    lv_obj_t *r2 = pf_wifi_row(box, "Access companion app",
                               "Scan QR to access Convoy,\nPhotos & other settings");

    // The pairing code lives HERE, next to the thing it unlocks, rather than on
    // a screen of its own. Wi-Fi and Images in the app act on one specific
    // board, and this is what tells the app which — and is the only proof it has
    // that the person typing it can see this screen.
    static char pair_sub[64];
    snprintf(pair_sub, sizeof(pair_sub), "Type this in the app to pair");
    lv_obj_t *r3 = pf_wifi_row_ex(box, pair_code_get(), pair_sub, PF_ROW_PLAIN);
    lv_obj_t *r3t = lv_obj_get_child(r3, 0);
    if (r3t) {
        lv_obj_set_style_text_font(r3t, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(r3t, lv_color_hex(0xFF6A00), 0);
        lv_obj_set_style_text_letter_space(r3t, 4, 0);
    }
    lv_obj_add_flag(r2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r2, pf_open_wifi_qr, LV_EVENT_CLICKED, NULL);
}

void pf_show_upload_overlay(bool wifi_only) {
    if (upload_overlay_open) return;
    upload_overlay_open = true;
    upload_overlay_opened_ms = lv_tick_get();
    pf_carousel_dirty = false;
    pf_overlay_wifi_only = wifi_only;

    pf_upload_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pf_upload_overlay, 466, 466);
    lv_obj_set_style_bg_color(pf_upload_overlay,
                              lv_color_hex(wifi_only ? 0xFFFFFF : 0x000000), 0);
    lv_obj_set_style_bg_opa(pf_upload_overlay, 255, 0);
    lv_obj_set_style_border_width(pf_upload_overlay, 0, 0);
    lv_obj_clear_flag(pf_upload_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(pf_upload_overlay);

    // Sized/positioned to match the photo long-press menu's close button
    // (PhotoFrameApp.cpp's photoframe_event_handler, size 60 at y=30) but a
    // bit bigger here since this is the primary/only control at the top of
    // an otherwise-empty area, not a secondary action in a button cluster.
    lv_obj_t * btn_close = lv_btn_create(pf_upload_overlay);
    lv_obj_set_size(btn_close, 72, 72);
    lv_obj_set_style_radius(btn_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(wifi_only ? 0x1A1A1A : 0x444444), 0);
    lv_obj_align(btn_close, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_ext_click_area(btn_close, 36);   // large invisible tap target beyond the visible circle
    lv_obj_move_foreground(btn_close);
    lv_obj_t * lbl_x = lv_label_create(btn_close);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_x);
    lv_obj_add_event_cb(btn_close, [](lv_event_t * ev) {
        lv_event_code_t c = lv_event_get_code(ev);
        if (c != LV_EVENT_CLICKED && c != LV_EVENT_RELEASED) return;
        if (lv_tick_elaps(upload_overlay_opened_ms) < 300) return;
        pf_close_upload_overlay();
    }, LV_EVENT_ALL, NULL);

    // Title removed as requested

    // Logo removed as requested

    // What this screen is FOR flips once the board has Wi-Fi. Before that, the
    // only thing that can work is joining the AP, so it shows the join QR and
    // the steps. After that, the useful destination is the app — and the phone
    // is off the AP by then, so a camera scan opens the normal browser with a
    // real internet route.
    //
    // That is the whole point of doing the handoff HERE rather than in the
    // portal page: the portal is served through a captive window the OS owns
    // and destroys with the Wi-Fi, and escaping it needs per-OS URL schemes
    // that behaved differently on every phone tried. A QR on the device has
    // none of that in the path.
    char cfg_ssids[8][33] = {};
    bool provisioned = wifi_only && (ota_list_networks(cfg_ssids, 8) > 0);

    // Drawn at NATIVE SIZE. The asset is 222x222 with a 4-module white quiet
    // zone baked in, which is what makes it scannable — the previous 200x200
    // asset had the finder pattern starting at pixel 0 (no quiet zone at all)
    // and was drawn at zoom 215, i.e. a 0.84x downscale. QR needs that border,
    // and resampling a bitmap QR by a non-integer factor smears module edges
    // until a camera cannot resolve them. Never scale this by a non-integer
    // factor; if it ever needs to be bigger, regenerate it or use 512 (2x).
    // Large variant where the toggle row is gone, small where it stays.
    // Sizes differ because the captions do. The provisioned screen says one
    // line, so the 259 QR fits under it; the setup screen carries three numbered
    // steps, and on a ROUND display the usable width collapses as you go down —
    // at y=420 there is only ~280px of glass. The 222 asset buys those rows.
    extern const lv_img_dsc_t wifi_qrcode;      // 222x222, 6px modules
    extern const lv_img_dsc_t app_qrcode;       // 259x259, 7px modules — the app
    lv_obj_t * qr_img = lv_img_create(pf_upload_overlay);
    lv_img_set_src(qr_img, provisioned ? &app_qrcode : &wifi_qrcode);
    lv_obj_align(qr_img, LV_ALIGN_CENTER, 0, provisioned ? 6 : (wifi_only ? -6 : -10));

    if (wifi_only && !provisioned) {
    // No toggle here: opening Wi-Fi setup IS the request to turn the hotspot on,
    // so asking again is a step with no decision behind it — and the row it
    // occupied is what lets the QR be comfortably scannable.
    //
    // Safe in THIS context specifically. The garble the toggle guards against is
    // a dropped full-frame flush, and that full redraw comes from
    // pf_invalidate_full_screen(), which early-returns unless the PHOTO screen is
    // active. From settings or first run it cannot happen at all. (The overflow
    // behind it is also fixed in amoled.cpp by banding transfers — see
    // WIFI_TOGGLE_INVESTIGATION.md — but this path does not depend on that.)
    pf_autostart_wifi = false;
    lv_timer_t * ap_start = lv_timer_create([](lv_timer_t * tm) {
        start_photoframe_wifi();
        lv_timer_del(tm);
    }, 400, NULL);
    (void)ap_start;
    } else if (provisioned) {
    // No AP at all on this branch. There is nothing to configure, and leaving
    // the hotspot up is what used to strand the phone on a network with no
    // internet — exactly what the app QR is here to avoid.
    pf_autostart_wifi = false;
    } else {
    // Wi-Fi Toggle Switch — wrapped in a flex row so the label+switch pair is
    // centered as a unit (previously the switch was hardcoded at center+50,
    // with the label hung off its left edge, so the whole row sat off-center
    // by however wide the label happened to render). Switch size matches the
    // Settings screen's toggles (60x30) for consistency.
    //
    // KEPT for the photo-upload path. Closing over the photo screen DOES trigger
    // the full-frame redraw that used to be dropped, so the trade that motivated
    // this toggle still applies here. When Firebase photo sync retires AP
    // uploads, this branch and the small QR variant go with it.
    lv_obj_t * wifi_row = lv_obj_create(pf_upload_overlay);
    lv_obj_set_size(wifi_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_row, 0, 0);
    lv_obj_set_style_border_width(wifi_row, 0, 0);
    lv_obj_set_style_pad_all(wifi_row, 0, 0);
    lv_obj_clear_flag(wifi_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wifi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wifi_row, 10, 0);
    lv_obj_align(wifi_row, LV_ALIGN_CENTER, 0, 130);

    lv_obj_t * wifi_lbl = lv_label_create(wifi_row);
    lv_label_set_text(wifi_lbl, "Enable Wi-Fi:");
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(wifi_lbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t * wifi_sw = lv_switch_create(wifi_row);
    lv_obj_set_size(wifi_sw, 60, 30);
    lv_obj_set_style_bg_color(wifi_sw, lv_color_hex(0xFF6A00), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(wifi_sw, [](lv_event_t * ev) {
        lv_obj_t * sw = lv_event_get_target(ev);
        if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            start_photoframe_wifi();
        } else {
            stop_photoframe_wifi();
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // First-run onboarding: open with the hotspot already ON.
    if (pf_autostart_wifi) {
        pf_autostart_wifi = false;
        lv_obj_add_state(wifi_sw, LV_STATE_CHECKED);
        // Defer the AP start briefly so it doesn't collide with LVGL rendering.
        lv_timer_t * t = lv_timer_create([](lv_timer_t * tm) {
            start_photoframe_wifi();
            lv_timer_del(tm);
        }, 400, NULL);
        (void)t;
    }
    }   // end !wifi_only

    // Caption. The unprovisioned case spells out the steps, because scanning a
    // Wi-Fi QR only gets the phone onto the AP — the setup page is a separate
    // move and users were left guessing at it.
    // Kept deliberately short. These sit low on a round screen where the glass
    // narrows fast, so every line has to clear the bezel at its own height.
    lv_obj_t * hint = lv_label_create(pf_upload_overlay);
    if (provisioned) {
        lv_label_set_text(hint, "Scan to open the app");
    } else if (wifi_only) {
        lv_label_set_text(hint, "1  Join Jimny_Dash_Sync\n"
                                "2  Setup page opens itself\n"
                                "3  Add your Wi-Fi");
    } else {
        lv_label_set_text(hint, "Wi-Fi: Jimny_Dash_Sync | Pass: password123");
    }
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(wifi_only ? 0x333333 : 0x888888), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(hint, 3, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, provisioned ? 158 : (wifi_only ? 140 : 168));

    lv_obj_t * cred_lbl = lv_label_create(pf_upload_overlay);
    lv_label_set_text(cred_lbl, provisioned ? CLOUD_URL
                                            : (wifi_only ? "Pass: password123" : "Visit 192.168.4.1"));
    lv_obj_set_style_text_color(cred_lbl, lv_color_hex(wifi_only ? 0xB34700 : 0xFF9800), 0);
    lv_obj_set_style_text_font(cred_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(cred_lbl, LV_ALIGN_CENTER, 0, provisioned ? 180 : (wifi_only ? 190 : 180));

    lv_obj_move_foreground(pf_upload_overlay);
    pf_invalidate_full_screen();
}

static void pf_close_upload_overlay(void) {
    if (!upload_overlay_open) return;
    upload_overlay_open = false;

    // Remove the overlay and force an immediate redraw FIRST so the close feels
    // instant — otherwise the blocking WiFi shutdown below makes it linger ~2s.
    if (pf_upload_overlay && lv_obj_is_valid(pf_upload_overlay)) {
        lv_obj_del(pf_upload_overlay);
    }
    pf_upload_overlay = NULL;
    lv_refr_now(NULL);

    // Now tear down the AP (this call can block for a second or two).
    stop_photoframe_wifi();

    pf_apply_carousel_refresh_after_upload();
}

static int pf_total_pages(void) {
    int n = (int)image_files.size();
    return (n == 0) ? 1 : (n + 4);
}

static bool pf_is_upload_page(int page_idx) {
    int n = (int)image_files.size();
    if (n == 0) return true;
    return (page_idx == 1 || page_idx == n + 2);
}

static int pf_map_page_to_img_idx(int page_idx) {
    int n = (int)image_files.size();
    if (n == 0) return -1;
    if (page_idx == 1 || page_idx == n + 2) return -1;
    if (page_idx == 0) return n - 1;
    if (page_idx == n + 3) return 0;
    return page_idx - 2;
}

static void pf_update_dots(int page_idx) {
    if (!pf_dot_cont) return;
    int n = (int)image_files.size();
    if (n == 0) return;
    
    int img_idx = pf_map_page_to_img_idx(page_idx);
    
    int dot_count = lv_obj_get_child_cnt(pf_dot_cont);
    for (int i = 0; i < dot_count; i++) {
        lv_obj_t * d = lv_obj_get_child(pf_dot_cont, i);
        bool active = (i == img_idx) && !pf_is_upload_page(page_idx);
        lv_obj_set_style_bg_color(d, lv_color_hex(active ? 0xFF9800 : 0x333333), 0);
    }
}

static void pf_style_page_img(lv_obj_t * img) {
    lv_obj_set_size(img, 466, 466);
    lv_obj_center(img);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
}

static int pf_find_slot(int img_idx) {
    for (int i = 0; i < PF_CACHE_SIZE; i++) {
        if (pf_buffer_loaded_index[i] == img_idx) return i;
    }
    return -1;
}

static int pf_get_free_slot(int current_idx) {
    int best_slot = 0;
    int max_dist = -1;
    for (int i = 0; i < PF_CACHE_SIZE; i++) {
        if (pf_buffer_loaded_index[i] == -1) return i;
        int dist = abs(pf_buffer_loaded_index[i] - current_idx);
        if (dist > max_dist) {
            max_dist = dist;
            best_slot = i;
        }
    }
    return best_slot;
}

static bool pf_cache_load(int img_idx, int current_idx) {
    if (img_idx < 0 || img_idx >= (int)image_files.size()) return false;
    if (pf_find_slot(img_idx) != -1) return true;
    
    int slot = pf_get_free_slot(current_idx);
    if (slot < 0 || slot >= PF_CACHE_SIZE) return false;
    
    String path = image_files[img_idx];
    FILE * f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // Do NOT memset to 0, which prevents the black flash issue
    fread(pf_buffers[slot], 1, 466 * 466 * 2, f);
    fclose(f);
    esp_cache_msync(pf_buffers[slot], 466 * 466 * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    pf_buffer_loaded_index[slot] = img_idx;

    pf_dscs[slot].header.always_zero = 0;
    pf_dscs[slot].header.w = 466;
    pf_dscs[slot].header.h = 466;
    pf_dscs[slot].header.cf = LV_IMG_CF_TRUE_COLOR;
    pf_dscs[slot].data_size = 466 * 466 * 2;
    pf_dscs[slot].data = pf_buffers[slot];
    lv_img_cache_invalidate_src(&pf_dscs[slot]);
    return true;
}

static void pf_bind_img_idx_to_pages(int img_idx) {
    if (!pf_pages || img_idx < 0) return;
    int n = (int)image_files.size();
    if (img_idx >= n) return;
    
    int slot = pf_find_slot(img_idx);
    if (slot == -1) return;
    
    int total = pf_total_pages();
    for (int i = 0; i < total; i++) {
        if (pf_map_page_to_img_idx(i) == img_idx) {
            lv_obj_t * pg = lv_obj_get_child(pf_pages, i);
            if (pg) {
                lv_obj_t * img = lv_obj_get_child(pg, 0);
                if (img) {
                    lv_img_set_src(img, &pf_dscs[slot]);
                    lv_obj_invalidate(pg);
                }
            }
        }
    }
}

static void pf_apply_carousel_refresh_after_upload(void) {
    scan_images();
    rebuild_pf_pages();
    int n = (int)image_files.size();
    lv_obj_clear_flag(pf_dot_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(pf_pages);

    int target = (n > 0) ? (n + 1) : 0;
    lv_obj_scroll_to_x(pf_pages, target * 466, LV_ANIM_OFF);
    if (n > 0) current_image_index = n - 1;
    pf_carousel_dirty = false;
    pf_pending_scroll_idx = -1;

    if (n > 0) {
        pf_load_page_immediately(target, true);
    } else {
        pf_invalidate_full_screen();
    }
    pf_update_dots(target);
}

static void pf_on_page_settled(int page_idx) {
    int n = (int)image_files.size();
    int total = pf_total_pages();
    if (page_idx < 0 || page_idx >= total) return;

    if (n > 0) {
        if (page_idx == 0) {
            page_idx = n + 1;
            lv_obj_scroll_to_x(pf_pages, page_idx * 466, LV_ANIM_OFF);
        } else if (page_idx == total - 1) {
            page_idx = 2;
            lv_obj_scroll_to_x(pf_pages, page_idx * 466, LV_ANIM_OFF);
        } else if (page_idx == total - 2) {
            page_idx = 1;
            lv_obj_scroll_to_x(pf_pages, page_idx * 466, LV_ANIM_OFF);
        }
    }

    static int last_settled = -1;
    bool was_upload = (last_settled >= 0) && pf_is_upload_page(last_settled);
    bool now_upload = pf_is_upload_page(page_idx);

    if (now_upload) {
        pf_pending_scroll_idx = -1;
        if (!was_upload) {
            // amoled.fillScreen(AMOLED_COLOR_BLACK);
        }
        pf_invalidate_full_screen();
    } else {
        if (was_upload && !upload_overlay_open) {
            // amoled.fillScreen(AMOLED_COLOR_BLACK);
        }
        pf_pending_scroll_idx = page_idx;
    }
    last_settled = page_idx;
}

static void pf_navigate_to_page(int page_idx) {
    if (!pf_pages) return;
    int total = pf_total_pages();
    if (page_idx < 0) page_idx = 0;
    if (page_idx >= total) page_idx = total - 1;

    bool entering_upload = pf_is_upload_page(page_idx);
    if (entering_upload) {
        // amoled.fillScreen(AMOLED_COLOR_BLACK);
    }

    if (!entering_upload) {
        int img_idx = pf_map_page_to_img_idx(page_idx);
        if (img_idx != -1) current_image_index = img_idx;
    }

    lv_obj_update_layout(pf_pages);
    lv_obj_scroll_to_x(pf_pages, (lv_coord_t)(page_idx * 466), LV_ANIM_OFF);
    pf_update_dots(page_idx);
    pf_on_page_settled(page_idx);
}

// --- Callbacks ---
static void photoframe_gesture_cb(lv_event_t * e) {
    if (upload_overlay_open) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_BOTTOM) {
            switch_to_launcher();
        }
    }
}

static void photoframe_event_handler(lv_event_t * e) {
    int curr_page = (lv_obj_get_scroll_x(pf_pages) + 233) / 466;
    if (upload_overlay_open || pf_is_upload_page(curr_page)) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_LONG_PRESSED) {
        if (!delete_dialog_open) {
            delete_dialog_open = true;
            menu_opened_time = lv_tick_get();
            lv_indev_wait_release(lv_indev_get_act()); // Force wait for release before handling any new click events
            
            lv_obj_t * overlay = lv_obj_create(photoframe_screen);
            lv_obj_set_size(overlay, 466, 466);
            lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(overlay, 210, 0);
            lv_obj_set_style_border_width(overlay, 0, 0);
            lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_center(overlay);

            lv_obj_t * btn_close = lv_btn_create(overlay);
            lv_obj_set_size(btn_close, 60, 60);
            lv_obj_set_style_radius(btn_close, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x444444), 0);
            lv_obj_align(btn_close, LV_ALIGN_TOP_MID, 0, 30);
            lv_obj_t * lbl_x = lv_label_create(btn_close);
            lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_20, 0); // 20% increased font size
            lv_obj_center(lbl_x);
            lv_obj_add_event_cb(btn_close, [](lv_event_t * ev) {
                if (lv_tick_elaps(menu_opened_time) < 300) return;
                lv_obj_t * ov = (lv_obj_t *)lv_event_get_user_data(ev);
                lv_obj_del(ov); delete_dialog_open = false;
                if (slideshow_active) {
                    last_image_change = millis();
                }
            }, LV_EVENT_CLICKED, overlay);

            lv_obj_t * cont = lv_obj_create(overlay);
            lv_obj_set_size(cont, 350, 260); 
            lv_obj_set_style_bg_opa(cont, 0, 0);
            lv_obj_set_style_border_width(cont, 0, 0);
            lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_center(cont);

            lv_obj_t * btn_slide = lv_btn_create(cont);
            lv_obj_set_size(btn_slide, 320, 60);
            lv_obj_set_style_bg_color(btn_slide, lv_color_hex(0x2196F3), 0);
            lv_obj_set_style_radius(btn_slide, 15, 0);
            lv_obj_t * lbl_slide = lv_label_create(btn_slide);
            lv_label_set_text(lbl_slide, slideshow_active ? "STOP SLIDE SHOW" : "START SLIDE SHOW");
            lv_obj_set_style_text_font(lbl_slide, &lv_font_montserrat_20, 0); // 20% increased font size
            lv_obj_center(lbl_slide);
            lv_obj_add_event_cb(btn_slide, [](lv_event_t * ev) {
                if (lv_tick_elaps(menu_opened_time) < 300) return;
                lv_obj_t * ov = (lv_obj_t *)lv_event_get_user_data(ev);
                slideshow_active = !slideshow_active; if(slideshow_active) last_image_change = millis();
                lv_obj_del(ov); delete_dialog_open = false;
            }, LV_EVENT_CLICKED, overlay);

            lv_obj_t * btn_boot = lv_btn_create(cont);
            lv_obj_set_size(btn_boot, 320, 60);
            lv_obj_set_style_bg_color(btn_boot, lv_color_hex(0xFF9800), 0);
            lv_obj_set_style_radius(btn_boot, 15, 0);
            lv_obj_t * lbl_boot = lv_label_create(btn_boot);
            lv_label_set_text(lbl_boot, "SET AS BOOTLOADER");
            lv_obj_set_style_text_font(lbl_boot, &lv_font_montserrat_20, 0); // 20% increased font size
            lv_obj_center(lbl_boot);
            lv_obj_add_event_cb(btn_boot, [](lv_event_t * ev) {
                if (lv_tick_elaps(menu_opened_time) < 300) return;
                lv_obj_t * ov = (lv_obj_t *)lv_event_get_user_data(ev);
                if (!image_files.empty() && current_image_index >= 0 &&
                    current_image_index < (int)image_files.size()) {
                    char boot_cfg[128]; snprintf(boot_cfg, sizeof(boot_cfg), "%s/boot_img.txt", SD_ROOT);
                    FILE* f = fopen(boot_cfg, "w");
                    if (f) {
                        fprintf(f, "%s", image_files[current_image_index].c_str());
                        fclose(f);
                    }
                }
                lv_obj_del(ov); delete_dialog_open = false;
            }, LV_EVENT_CLICKED, overlay);

            lv_obj_t * btn_del = lv_btn_create(cont);
            lv_obj_set_size(btn_del, 320, 60);
            lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x555555), 0);
            lv_obj_set_style_radius(btn_del, 15, 0);
            lv_obj_t * lbl_del = lv_label_create(btn_del);
            lv_label_set_text(lbl_del, "DELETE IMAGE");
            lv_obj_set_style_text_font(lbl_del, &lv_font_montserrat_20, 0); // 20% increased font size
            lv_obj_center(lbl_del);
            lv_obj_add_event_cb(btn_del, [](lv_event_t * ev) {
                if (lv_tick_elaps(menu_opened_time) < 300) return;
                lv_obj_t * ov = (lv_obj_t *)lv_event_get_user_data(ev);
                delete_current_image();
                lv_obj_del(ov); delete_dialog_open = false;
            }, LV_EVENT_CLICKED, overlay);
        }
    }
}

void build_photoframe_screen() {
    photoframe_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(photoframe_screen, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(photoframe_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(photoframe_screen, photoframe_gesture_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(photoframe_screen, photoframe_event_handler, LV_EVENT_LONG_PRESSED, NULL);

    // ── Horizontal snap page container (fills whole screen, one page per image) ──
    pf_pages = lv_obj_create(photoframe_screen);
    lv_obj_set_size(pf_pages, 466, 466);
    lv_obj_set_pos(pf_pages, 0, 0);
    lv_obj_set_flex_flow(pf_pages, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_snap_x(pf_pages, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pf_pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(pf_pages, 0, 0);
    lv_obj_set_style_border_width(pf_pages, 0, 0);
    lv_obj_set_style_pad_all(pf_pages, 0, 0);
    lv_obj_set_style_pad_column(pf_pages, 0, 0);
    lv_obj_set_style_bg_color(pf_pages, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pf_pages, LV_OPA_COVER, 0);
    lv_obj_add_flag(pf_pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(pf_pages, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(pf_pages, LV_DIR_HOR);

    // Register event handlers directly on pf_pages since it intercepts all screen gestures/clicks
    lv_obj_add_event_cb(pf_pages, photoframe_gesture_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(pf_pages, photoframe_event_handler, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_add_event_cb(pf_pages, [](lv_event_t * ev) {
        if (upload_overlay_open) return;
        lv_obj_t * pages = lv_event_get_target(ev);
        int total = pf_total_pages();
        if (total <= 0) return;
        lv_coord_t scroll_x = lv_obj_get_scroll_x(pages);
        int page_idx = (scroll_x + 233) / 466;
        if (page_idx < 0) page_idx = 0;
        if (page_idx >= total) page_idx = total - 1;
        last_scroll_time = millis();
        pf_update_dots(page_idx);
    }, LV_EVENT_SCROLL, NULL);

    lv_obj_add_event_cb(pf_pages, [](lv_event_t * ev) {
        if (upload_overlay_open) return;
        lv_obj_t * pages = lv_event_get_target(ev);
        int total = pf_total_pages();
        if (total <= 0) return;
        lv_coord_t scroll_x = lv_obj_get_scroll_x(pages);
        int page_idx = (scroll_x + 233) / 466;
        if (page_idx < 0) page_idx = 0;
        if (page_idx >= total) page_idx = total - 1;
        last_scroll_time = millis();
        if (!pf_is_upload_page(page_idx)) {
            int img_idx = pf_map_page_to_img_idx(page_idx);
            if (img_idx != -1 && img_idx != current_image_index) {
                current_image_index = img_idx;
                last_image_change = millis();
            }
        }
        pf_on_page_settled(page_idx);
    }, LV_EVENT_SCROLL_END, NULL);

    // ── Pagination dot bar ────────────────────────────────────────────────
    pf_dot_cont = lv_obj_create(photoframe_screen);
    lv_obj_set_size(pf_dot_cont, 200, 14);
    lv_obj_align(pf_dot_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(pf_dot_cont, 0, 0);
    lv_obj_set_style_border_width(pf_dot_cont, 0, 0);
    lv_obj_set_flex_flow(pf_dot_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pf_dot_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pf_dot_cont, 6, 0);
    lv_obj_clear_flag(pf_dot_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pf_dot_cont, LV_OBJ_FLAG_HIDDEN);
}

void scan_images() {
    image_files.clear();
    DIR *dir = opendir(SD_ROOT);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        String filename = ent->d_name;
        if (filename.endsWith(".bin")) {
            char full_path[256]; snprintf(full_path, sizeof(full_path), "%s/%s", SD_ROOT, ent->d_name);
            image_files.push_back(String(full_path));
        }
    }
    closedir(dir);
    std::sort(image_files.begin(), image_files.end());
}

static void pf_load_page_immediately(int page_idx, bool load_neighbors) {
    int n = (int)image_files.size();
    if (!pf_pages || n <= 0 || page_idx < 0 || page_idx >= pf_total_pages()) return;

    int center_img_idx = pf_map_page_to_img_idx(page_idx);
    if (center_img_idx == -1) {
        if (load_neighbors) {
            if (pf_cache_load(0, -1)) pf_bind_img_idx_to_pages(0);
            if (pf_cache_load(n - 1, -1)) pf_bind_img_idx_to_pages(n - 1);
        }
        pf_invalidate_full_screen();
        return;
    }
    
    int start_off = load_neighbors ? -1 : 0;
    int end_off = load_neighbors ? 1 : 0;
    for (int offset = start_off; offset <= end_off; offset++) {
        int img_idx = center_img_idx + offset;
        if (img_idx < 0) img_idx = n - 1;
        if (img_idx >= n) img_idx = 0;
        
        if (pf_cache_load(img_idx, center_img_idx)) {
            pf_bind_img_idx_to_pages(img_idx);
        }
    }
    pf_invalidate_full_screen();
}

void switch_to_photoframe() {
    upload_overlay_open = false;
    pf_upload_overlay = NULL;
    pf_pending_scroll_idx = -1;
    pf_carousel_dirty = false;
    scan_images();
    rebuild_pf_pages();

    int n = (int)image_files.size();
    lv_obj_clear_flag(pf_dot_cont, LV_OBJ_FLAG_HIDDEN);
    if (n > 0) {
        if (current_image_index < 0 || current_image_index >= n) {
            current_image_index = 0;
        }
        pf_navigate_to_page(current_image_index + 2);
    } else {
        pf_navigate_to_page(0);
    }

    lv_scr_load_anim(photoframe_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

// Carousel: image pages 0..n-1, then upload page (swipe left to reach upload).
static void rebuild_pf_pages() {
    if (!pf_pages) return;

    for (int i = 0; i < PF_CACHE_SIZE; i++) pf_buffer_loaded_index[i] = -1;
    lv_obj_clean(pf_pages);
    lv_obj_clean(pf_dot_cont);

    int n = (int)image_files.size();
    int total = pf_total_pages();

    for (int i = 0; i < total; i++) {
        lv_obj_t * pg = lv_obj_create(pf_pages);
        lv_obj_set_size(pg, 466, 466);
        lv_obj_set_style_bg_color(pg, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(pg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pg, 0, 0);
        lv_obj_set_style_pad_all(pg, 0, 0);
        lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        if (pf_is_upload_page(i)) {
            pf_build_upload_gateway_page(pg);
        } else {
            lv_obj_t * img = lv_img_create(pg);
            pf_style_page_img(img);
        }
    }

    // Dots logic
    lv_obj_clear_flag(pf_dot_cont, LV_OBJ_FLAG_HIDDEN);
    int shown = n > 30 ? 30 : n;
    lv_obj_set_width(pf_dot_cont, (lv_coord_t)(shown * 16));
    for (int i = 0; i < shown; i++) {
        lv_obj_t * d = lv_obj_create(pf_dot_cont);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == 0 ? 0xFF9800 : 0x333333), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

void show_image(int img_idx) {
    int n = (int)image_files.size();
    if (n <= 0 || img_idx < 0 || img_idx >= n) return;
    last_image_change = millis();
    pf_navigate_to_page(img_idx + 2);
}

void next_image() {
    int n = (int)image_files.size();
    if (n <= 0) return;
    int next = current_image_index + 1;
    if (next >= n) next = 0;
    show_image(next);
}

void prev_image() {
    int n = (int)image_files.size();
    if (n <= 0) return;
    int prev = current_image_index - 1;
    if (prev < 0) prev = n - 1;
    show_image(prev);
}

void delete_current_image() {
    if (image_files.empty()) return;
    int img_idx = current_image_index;
    if (img_idx >= 0 && img_idx < (int)image_files.size()) {
        unlink(image_files[img_idx].c_str());
    }
    scan_images();

    int n = (int)image_files.size();
    rebuild_pf_pages();

    if (n == 0) {
        pf_navigate_to_page(0);
    } else {
        if (current_image_index >= n) current_image_index = n - 1;
        pf_navigate_to_page(current_image_index + 2);
    }
}

void photoframe_setup() {
    for (int i = 0; i < PF_CACHE_SIZE; i++) {
        if (!pf_buffers[i]) pf_buffers[i] = (uint8_t *)heap_caps_aligned_alloc(64, 466 * 466 * 2, MALLOC_CAP_SPIRAM);
        if (pf_buffers[i]) {
            pf_dscs[i].header.always_zero = 0; pf_dscs[i].header.w = 466; pf_dscs[i].header.h = 466;
            pf_dscs[i].header.cf = LV_IMG_CF_TRUE_COLOR; pf_dscs[i].data_size = 466 * 466 * 2;
            pf_dscs[i].data = pf_buffers[i];
        }
    }

    // The portal is Wi-Fi provisioning and nothing else — that is the only thing
    // that CAN work here, because a phone on this AP has no route to the
    // internet. Convoy and settings moved to the cloud PWA (see
    // docs/CLIENT_UNIFICATION_PLAN.md); /photos survives unadvertised as the
    // offline upload fallback until cloud photo sync replaces it.
    photo_server.on("/", HTTP_GET, []() { photo_server.sendHeader("Location", "/wifi"); photo_server.send(302); });
    photo_server.on("/photos", HTTP_GET, []() { Serial.println("[PF] req /photos"); photo_server.send(200, "text/html", index_html); });
    photo_server.on("/upload", HTTP_POST, []() { photo_server.send(200, "text/plain", "OK"); }, []() {
        HTTPUpload& upload = photo_server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            char path[128]; snprintf(path, sizeof(path), "%s/%s", SD_ROOT, upload.filename.c_str());
            uploadFile = fopen(path, "wb");
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) fwrite(upload.buf, 1, upload.currentSize, uploadFile);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) { fclose(uploadFile); uploadFile = NULL; }
            new_image_uploaded = true;
        }
    });

    // ── WiFi Network Setup endpoints (for OTA) ────────────────────────────────
    photo_server.on("/wifi", HTTP_GET, []() {
        Serial.printf("[PF] req /wifi host='%s'\n", photo_server.hostHeader().c_str());
        // Collect saved networks for display
        char ssids[8][33] = {};
        int count = ota_list_networks(ssids, 8);
        String net_list = "";
        for (int i = 0; i < count; i++) {
            String s = html_escape(ssids[i]);
            net_list += "<li style='padding:6px 0;border-bottom:1px solid #333;'>";
            net_list += "<span style='color:#eee;'>" + s + "</span>";
            net_list += "<form style='display:inline;margin-left:12px;' method='POST' action='/wifi/remove'>";
            net_list += "<input type='hidden' name='ssid' value='" + s + "'>";
            net_list += "<button style='background:#c0392b;color:#fff;border:none;padding:4px 10px;border-radius:6px;cursor:pointer;'>Remove</button>";
            net_list += "</form></li>";
        }
        if (net_list.isEmpty()) net_list = "<li style='color:#666;'>No networks saved yet</li>";

        // The handoff to the cloud PWA — the one place the portal points at it.
        // Shown whenever the board HAS a network, not only in the moment one is
        // saved: any visit with credentials already stored is a visit where the
        // useful next step is the app, and a user who came back to add a second
        // network should not have to save something to be told where to go.
        //
        // It points at the DEVICE SCREEN rather than offering a link. This page
        // is served through the OS captive-portal window, which the OS destroys
        // along with the Wi-Fi — so nothing here can survive long enough to
        // carry the phone to the app. The board's own screen can: it switches
        // to an app QR, the hotspot goes away, the phone returns to mobile data,
        // and a camera scan lands in the normal browser with a working route.
        bool just_saved = photo_server.hasArg("ok");
        String done = "";
        if (just_saved || count > 0) {
            done = "<div class='card done'>";
            if (just_saved) {
                done += "<h2 class='ok'>&#10003; Saved &mdash; ";
                done += html_escape(photo_server.arg("ok").c_str());
                done += "</h2><p class='l'>The board joins this network by itself from now on, "
                        "and its hotspot switches off in a few seconds.";
            } else {
                done += "<h2 class='ok'>&#10003; This board is set up</h2>"
                        "<p class='l'>It already has Wi-Fi saved.";
            }
            done += " Everything else &mdash; convoy, callsign, photos &mdash; lives in the app.</p>"
                    "<div class='url'>Scan the QR on the device screen</div>"
                    "<p class='l'>The screen is showing a code for <b>";
            done += CLOUD_URL;
            done += "</b>. Scan it with your camera once this page stops responding &mdash; by "
                    "then your phone is back on mobile data and the link will load.</p></div>";
        }

        String page = R"rawliteral(
<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Trailmaster Sync — WiFi Setup</title>
<style>
  :root { --primary: #e67e22; --bg: #121212; --card: #1e1e1e; --text: #f5f5f5; }
  body{margin:0;background:var(--bg);color:var(--text);font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;display:flex;flex-direction:column;align-items:center;padding:0;}
  .header { background: #000; width: 100%; padding: 20px 0; text-align: center; border-bottom: 3px solid var(--primary); margin-bottom: 20px; }
  .header h1 { margin: 0; font-size: 24px; letter-spacing: 2px; text-transform: uppercase; display: flex; align-items: center; justify-content: center; gap: 10px; }
  .container { padding: 0 20px 20px 20px; width: 100%; max-width: 440px; box-sizing: border-box; }
  p{color:#aaa;font-size:14px;margin:0 0 20px;text-align:center;}
  .l{text-align:left;}
  .card{background:var(--card);border-radius:12px;padding:25px;width:100%;box-shadow: 0 8px 16px rgba(0,0,0,0.5);box-sizing:border-box;margin-bottom:20px;}
  .card h2{font-size:16px;color:var(--primary);margin:0 0 14px;text-align:left;}
  .done{border:1px solid var(--primary);}
  .done h2.ok{color:#2ecc71;}
  /* 222 = 37 modules x 6px. Any non-integer factor smears the module edges past
     what a phone camera can resolve — same rule as the on-screen QR. */
  .qr{display:block;margin:0 auto 14px;width:222px;height:222px;background:#fff;border-radius:6px;
      image-rendering:-webkit-optimize-contrast;image-rendering:pixelated;}
  a.url{display:block;background:#000;padding:16px;border-radius:8px;text-align:center;font-size:18px;
        letter-spacing:1px;color:#9cf;margin-bottom:14px;text-decoration:none;border:1px solid #234;}
  a.url:active{background:#101820;}
  ul{list-style:none;margin:0;padding:0;text-align:left;}
  input{width:100%;box-sizing:border-box;background:#111;border:1px solid #444;color:#eee;padding:12px;border-radius:8px;font-size:16px;margin-bottom:15px;}
  button.primary{width:100%;background:var(--primary);color:#fff;border:none;padding:15px;border-radius:8px;font-size:16px;font-weight:bold;text-transform:uppercase;cursor:pointer;transition:0.3s;}
  button.primary:hover{background:#d35400;}
  .foot{display:block;text-align:center;color:#555;font-size:12px;text-decoration:none;}
</style></head><body>
  <div class="header">
      <h1>
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="var(--primary)" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"></path><circle cx="12" cy="10" r="3"></circle></svg>
          TRAILMASTER
      </h1>
  </div>
  <div class="container">
)rawliteral";
        page += done;
        page += R"rawliteral(
      <p>Add your home WiFi or phone hotspot. The device will connect automatically when you check for updates.</p>
      <div class='card'>
        <h2>Saved Networks</h2>
        <ul id='netlist'>)rawliteral";
        page += net_list;
        page += R"rawliteral(
  </ul>
</div>
<div class='card'>
  <h2>Add a Network</h2>
  <form method='POST' action='/wifi/add'>
    <input type='text' name='ssid' placeholder='WiFi Network Name (SSID)' required>
    <input type='text' name='pass' placeholder='Password'>
    <button class='primary' type='submit'>Sync to Device</button>
  </form>
</div>
<a class='foot' href='/photos'>Send photos over this Wi-Fi</a>
  </div>
</body></html>)rawliteral";
        photo_server.send(200, "text/html", page);
    });

    photo_server.on("/wifi/add", HTTP_POST, []() {
        String ssid = photo_server.arg("ssid");
        String pass = photo_server.arg("pass");
        if (ssid.length() > 0) {
            ota_add_network(ssid.c_str(), pass.c_str());
            // 8 s: long enough to follow the redirect and read the success card,
            // short enough that the user is not left sitting on a dead network.
            pf_provisioned_at = millis() + 8000;
            Serial.printf("[PF] '%s' saved — app QR in 8s\n", ssid.c_str());
            photo_server.sendHeader("Location", "/wifi?ok=" + url_encode(ssid));
            photo_server.send(303);
        } else {
            photo_server.send(400, "text/plain", "SSID required");
        }
    });

    // The handoff. The hard part is not the redirect, it is WHICH WINDOW we are
    // in: joining a captive network opens the OS's portal window (Android's
    // CaptivePortalLogin, iOS's Captive Network Assistant), and that window is
    // owned by the Wi-Fi session. Drop the AP and the window is destroyed — which
    // is why waiting inside it to forward somewhere could never work. The page
    // has to escape to the phone's real browser first, and the only lever for
    // that is a platform URL scheme.
    //
    // Both platforms escape to this same page with ?go=1. The escape buys only
    // one thing — a window that outlives the Wi-Fi — and never the app itself:
    // on the AP our own DNS answers for every host, so aiming a browser at the
    // app URL just re-renders the setup page. (Measured, not assumed: Chrome on
    // a Pixel rides the AP the same way Safari does.) The ?go=1 copy, running in
    // that surviving window, arms the teardown and waits the AP out.
    //
    photo_server.on("/wifi/remove", HTTP_POST, []() {
        String ssid = photo_server.arg("ssid");
        if (ssid.length() > 0) {
            ota_remove_network(ssid.c_str());
        }
        photo_server.sendHeader("Location", "/wifi");
        photo_server.send(303);
    });

    // ── Captive portal ────────────────────────────────────────────────────────
    // Nothing can make a phone JOIN a network without the user agreeing — that
    // consent is an OS guarantee and the QR's WIFI: payload already buys the
    // one-tap prompt. What we CAN do is skip the "now type 192.168.4.1" step
    // after they join.
    //
    // Every OS probes a known URL the moment it joins and decides a portal is
    // present when the answer is not the one it expected: Android wants a bare
    // 204 from /generate_204, iOS and macOS want a specific Success page from
    // /hotspot-detect.html, Windows wants known text from /ncsi.txt. A 302 is
    // none of those, so all of them conclude "portal" and open a browser on
    // whatever we point at. photo_dnsServer already answers every hostname with
    // our own IP, so each of those probes arrives here as an unknown path.
    photo_server.onNotFound([]() {
        // Logged with the Host header, because that is the one thing that says
        // WHO the browser thought it was talking to — a probe, our own IP, or a
        // hijacked lookup of the app's hostname.
        Serial.printf("[PF] req 404 host='%s' uri='%s'\n",
                      photo_server.hostHeader().c_str(), photo_server.uri().c_str());
        photo_server.sendHeader("Location", "http://192.168.4.1/wifi", true);
        photo_server.send(302, "text/plain", "");
    });

    photo_server.on("/wifi/list", HTTP_GET, []() {
        char ssids[8][33] = {};
        int count = ota_list_networks(ssids, 8);
        String json = "[";
        for (int i = 0; i < count; i++) {
            if (i) json += ",";
            json += "\"" + String(ssids[i]) + "\"";
        }
        json += "]";
        photo_server.send(200, "application/json", json);
    });

    // WebServer discards headers it was not told to keep; the handoff logging
    // wants User-Agent to tell the captive window apart from the real browser.
    static const char * kept[] = { "User-Agent" };
    photo_server.collectHeaders(kept, 1);
}

void start_photoframe_wifi() {
    if (wifi_ap_running) return;
    wifi_ap_running = true; // Signal OBD task to stop touching WiFi immediately
    delay(500); // Wait for OBD task to exit its WiFi manipulation cycle
    
    // Completely reset WiFi radio
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4); // Force Channel 1 to ensure visibility
    photo_dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    photo_server.begin(); wifi_ap_running = true;
    Serial.println("Photo Frame Sync Active: http://192.168.4.1");
    Serial.println("WiFi Setup: http://192.168.4.1/wifi");
}

void stop_photoframe_wifi() {
    if (!wifi_ap_running) return;
    photo_server.stop(); photo_dnsServer.stop();
    WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
    wifi_ap_running = false;
}

void photoframe_loop_handler() {
    if (new_image_uploaded) {
        new_image_uploaded = false;
        if (upload_overlay_open) {
            pf_carousel_dirty = true;
        } else {
            int old_n = (int)image_files.size();
            scan_images();
            int new_n = (int)image_files.size();
            if (new_n != old_n) {
                rebuild_pf_pages();
                if (new_n > 0) {
                    if (current_image_index >= new_n) current_image_index = new_n - 1;
                    pf_navigate_to_page(current_image_index + 2);
                } else {
                    pf_navigate_to_page(0);
                }
            }
        }
    }

    if (!upload_overlay_open && pf_pending_scroll_idx >= 0 &&
        (millis() - last_scroll_time > 120)) {
        int idx = pf_pending_scroll_idx;
        pf_pending_scroll_idx = -1;
        pf_load_page_immediately(idx, false);
        pf_update_dots(idx);
    }

    if (wifi_ap_running) { photo_dnsServer.processNextRequest(); photo_server.handleClient(); }

    // Armed by /wifi/add. Rebuilding the overlay is what swaps the QR: reopening
    // it re-reads the saved networks, so it comes back in its provisioned form —
    // the app code, and no AP. Closing first is what actually stops the hotspot,
    // and it also means a join-me QR is never left on screen for a network that
    // no longer exists.
    if (pf_provisioned_at && (int32_t)(millis() - pf_provisioned_at) >= 0) {
        pf_provisioned_at = 0;
        Serial.println("[PF] provisioned: AP down, screen now shows the app QR");
        if (upload_overlay_open && pf_overlay_wifi_only) {
            pf_close_upload_overlay();
            pf_show_upload_overlay(true);
        } else {
            stop_photoframe_wifi();
        }
    }

    int curr_page = (lv_obj_get_scroll_x(pf_pages) + 233) / 466;
    if (lv_scr_act() == photoframe_screen && slideshow_active && !delete_dialog_open &&
        !upload_overlay_open && !pf_is_upload_page(curr_page)) {
        int n = (int)image_files.size();
        if (n > 1 && (millis() - last_image_change >= SLIDESHOW_INTERVAL)) {
            next_image();
        }
    }
}

__attribute__((hot)) __attribute__((optimize("O3")))
static uint8_t * splash_canvas_buf = NULL;

static int splash_dirty_x1 = 466, splash_dirty_y1 = 466;
static int splash_dirty_x2 = 0,   splash_dirty_y2 = 0;
static bool splash_clear_next = false;

static void SplashGIFDraw(GIFDRAW *pDraw) {
    if (!splash_canvas_buf) return;
    
    // Handle Disposal Method 2 (Restore to Background) on the first scanline of the new frame
    if (splash_clear_next && pDraw->y == 0) {
        memset(splash_canvas_buf, 0, 466 * 466 * 2);
        splash_clear_next = false;
        // Invalidate full screen to ensure the cleared background is flushed
        splash_dirty_x1 = 0; splash_dirty_x2 = 465;
        splash_dirty_y1 = 0; splash_dirty_y2 = 465;
    }
    
    int x, y, iWidth;
    iWidth = pDraw->iWidth;
    if (pDraw->iX + iWidth > 466) iWidth = 466 - pDraw->iX;
    if (iWidth <= 0) return;
    y = pDraw->iY + pDraw->y;
    if (y >= 466 || pDraw->iX >= 466) return;
    
    // Track dirty bounding box for this frame
    if (pDraw->iX < splash_dirty_x1) splash_dirty_x1 = pDraw->iX;
    if (pDraw->iX + iWidth - 1 > splash_dirty_x2) splash_dirty_x2 = pDraw->iX + iWidth - 1;
    if (y < splash_dirty_y1) splash_dirty_y1 = y;
    if (y > splash_dirty_y2) splash_dirty_y2 = y;
    uint8_t *s = pDraw->pPixels;
    lv_color_t * line_ptr = (lv_color_t*)splash_canvas_buf + (y * 466) + pDraw->iX;
    uint16_t *usPalette = pDraw->pPalette;
    if (pDraw->ucHasTransparency) {
        uint8_t ucT = pDraw->ucTransparent;
        for (x = 0; x < iWidth; x++) {
            uint8_t c = *s++;
            if (c != ucT) *line_ptr = (lv_color_t){.full = usPalette[c]};
            line_ptr++;
        }
    } else {
        for (x = 0; x < iWidth; x++) {
            *line_ptr++ = (lv_color_t){.full = usPalette[*s++]};
        }
    }
    
    // Check if this frame requests to be cleared before the NEXT frame is drawn
    if (pDraw->y == pDraw->iHeight - 1) {
        if (pDraw->ucDisposalMethod == 2) {
            splash_clear_next = true;
        }
    }
}

lv_obj_t * show_boot_splash() {
    // Note: SD_card_Init() is already called in setup(), do not call it twice!
    
    // Ensure PSRAM buffer is allocated for custom splash
    if (!pf_buffers[0]) {
        pf_buffers[0] = (uint8_t *)heap_caps_aligned_alloc(64, 466 * 466 * 2, MALLOC_CAP_SPIRAM);
        if (pf_buffers[0]) {
            pf_dscs[0].header.always_zero = 0; pf_dscs[0].header.w = 466; pf_dscs[0].header.h = 466;
            pf_dscs[0].header.cf = LV_IMG_CF_TRUE_COLOR; pf_dscs[0].data_size = 466 * 466 * 2;
            pf_dscs[0].data = pf_buffers[0];
        }
    }

    lv_obj_t * splash_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash_scr, lv_color_hex(0x000000), 0);
    lv_obj_t * splash_content = lv_img_create(splash_scr);
    
    bool custom_splash_loaded = false;
    unsigned long delay_time = get_custom_boot_time() * 1000;
    
    char boot_cfg[128]; snprintf(boot_cfg, sizeof(boot_cfg), "%s/boot_img.txt", SD_ROOT);
    FILE* f_cfg = fopen(boot_cfg, "r");
    if (f_cfg) {
        char img_path[128];
        if (fgets(img_path, sizeof(img_path), f_cfg)) {
            // Trim newline
            img_path[strcspn(img_path, "\r\n")] = 0;
            FILE* f_img = fopen(img_path, "rb");
            if (f_img && pf_buffers[0]) {
                size_t read_bytes = fread(pf_buffers[0], 1, 466 * 466 * 2, f_img);
                if (read_bytes == 466 * 466 * 2) {
                    esp_cache_msync(pf_buffers[0], 466 * 466 * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
                    lv_img_set_src(splash_content, &pf_dscs[0]);
                    custom_splash_loaded = true;
                }
                fclose(f_img);
            }
        }
        fclose(f_cfg);
    }

    uint8_t* gif_psram_buffer = NULL; // New local dynamic buffer for GIF
    bool gif_is_playing = false;
    AnimatedGIF *splash_gif = new AnimatedGIF();

    if (!custom_splash_loaded) {
        // If Jimny look is OFF, show Trailmaster logo on black for 3 seconds
        extern int use_jimny_logo;
        extern const lv_img_dsc_t ui_img_trailmaster;
        if (!use_jimny_logo) {
            lv_img_set_src(splash_content, &ui_img_trailmaster);
            lv_img_set_zoom(splash_content, 380);
            lv_obj_center(splash_content);
            lv_scr_load(splash_scr);
            unsigned long start_tm = millis();
            unsigned long last_tick_tm = millis();
            while (millis() - start_tm < 3000) {
                unsigned long ct = millis();
                lv_tick_inc(ct - last_tick_tm);
                last_tick_tm = ct;
                lv_timer_handler();
                ::delay(10);
                yield();
            }
            delete splash_gif;
            return splash_scr;
        }

        char real_gif_path[128]; snprintf(real_gif_path, sizeof(real_gif_path), "%s/splash.gif", SD_ROOT);
        struct stat st;
        Serial.printf("[BOOT] Checking for GIF at: %s\n", real_gif_path);
        
        if (stat(real_gif_path, &st) == 0 && st.st_size > 0) {
            Serial.printf("[BOOT] Found splash.gif, size: %ld bytes. Allocating PSRAM buffer...\n", st.st_size);
            gif_psram_buffer = (uint8_t *)heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM);
            
            if (gif_psram_buffer) {
                FILE* f_gif = fopen(real_gif_path, "rb");
                if (f_gif) {
                    size_t read_bytes = fread(gif_psram_buffer, 1, st.st_size, f_gif);
                    fclose(f_gif);
                    if (read_bytes == st.st_size) {
                        lv_obj_del(splash_content);
                        splash_content = lv_canvas_create(splash_scr);
                        splash_canvas_buf = (uint8_t *)heap_caps_malloc(466 * 466 * 2, MALLOC_CAP_SPIRAM);
                        if (splash_canvas_buf) {
                            lv_canvas_set_buffer(splash_content, splash_canvas_buf, 466, 466, LV_IMG_CF_TRUE_COLOR);
                            lv_canvas_fill_bg(splash_content, lv_color_hex(0x000000), LV_OPA_COVER);
                            
                            splash_gif->begin(LITTLE_ENDIAN_PIXELS);
                            if (splash_gif->open(gif_psram_buffer, (int)st.st_size, SplashGIFDraw)) {
                                gif_is_playing = true;
                                Serial.println("[BOOT] AnimatedGIF Decoder armed, entering render loop.");
                            }
                        } else {
                            Serial.println("[BOOT] Failed to allocate canvas buffer for GIF.");
                        }
                    } else {
                        heap_caps_free(gif_psram_buffer);
                        gif_psram_buffer = NULL;
                    }
                } else {
                    heap_caps_free(gif_psram_buffer);
                    gif_psram_buffer = NULL;
                }
            } else {
                 Serial.println("[BOOT] Failed to allocate PSRAM for GIF!");
            }
        } 
        
        // If GIF loading failed or didn't exist, we still need a fallback
        if (!gif_is_playing) {
            Serial.println("[BOOT] splash.gif not loaded, falling back to static Jimny Logo.");
            extern const lv_img_dsc_t Jimnylogo;
            lv_img_set_src(splash_content, &Jimnylogo);
            lv_img_set_zoom(splash_content, 380);
        }
    }
    
    lv_obj_center(splash_content);
    lv_scr_load(splash_scr);
    
    unsigned long start = millis();
    unsigned long last_tick = millis();
    
    if (gif_is_playing) {
        int delay = 0;
        unsigned long next_frame_time = millis();
        
        // Loop exactly until the GIF finishes its sequence once
        while (splash_gif->playFrame(false, &delay)) {
            if (splash_dirty_x2 >= splash_dirty_x1 && splash_dirty_y2 >= splash_dirty_y1) {
                lv_area_t dirty_area = {
                    (lv_coord_t)splash_dirty_x1, (lv_coord_t)splash_dirty_y1,
                    (lv_coord_t)splash_dirty_x2, (lv_coord_t)splash_dirty_y2
                };
                lv_obj_invalidate_area(splash_content, &dirty_area);
            }
            
            // Reset bounds for the next frame
            splash_dirty_x1 = 466; splash_dirty_y1 = 466;
            splash_dirty_x2 = 0;   splash_dirty_y2 = 0;
            
            unsigned long current_time = millis();
            lv_tick_inc(current_time - last_tick);
            last_tick = current_time;
            
            lv_timer_handler();
            
            int frame_delay = (delay > 0) ? delay : 30;
            next_frame_time += frame_delay;
            
            long time_to_wait = next_frame_time - millis();
            if (time_to_wait > 0) {
                ::delay(time_to_wait);
            } else {
                // We are lagging behind, don't delay but yield to prevent watchdog
                yield();
            }
        }
    } else {
        // Fallback for custom boot image or static logo uses normal timeout
        while (millis() - start < delay_time) { 
            lv_tick_inc(10);
            lv_timer_handler(); 
            ::delay(10);
            yield();
        }
    }
    
    Serial.println("[BOOT] Splash playback complete, transferring to main UI.");
    
    if (gif_is_playing && splash_canvas_buf) {
        splash_gif->close();
        lv_obj_del(splash_content);
        heap_caps_free(splash_canvas_buf);
        splash_canvas_buf = NULL;
    }
    
    delete splash_gif;
    
    if (gif_psram_buffer) {
        heap_caps_free(gif_psram_buffer);
    }

    return splash_scr;
}

int get_custom_boot_time() {
    FILE* f = fopen("/sd_card/boot_time.txt", "r");
    int val = 3; // Default to 3 seconds
    if (f) {
        if (fscanf(f, "%d", &val) != 1) {
            val = 3;
        }
        fclose(f);
        if (val < 3) val = 3;
        if (val > 8) val = 8;
    }
    return val;
}

void set_custom_boot_time(int val) {
    if (val < 3) val = 3;
    if (val > 8) val = 8;
    FILE* f = fopen("/sd_card/boot_time.txt", "w");
    if (f) {
        fprintf(f, "%d", val);
        fclose(f);
    }
}
