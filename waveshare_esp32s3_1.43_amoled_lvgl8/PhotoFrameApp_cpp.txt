#include "PhotoFrameApp.h"
#include "jimnypod.h"
#include <FFat.h>
#include <WiFi.h>

// --- HTML Frontend ---
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
        <div class="card">
            <div class="warning-banner" id="macWarning">
                <strong>Notice:</strong> If tapping the button below does nothing, please close this popup window, open <b>Safari</b> or <b>Chrome</b>, and go to <b>http://trailmaster.io</b>
            </div>
            <p style="color: #aaa; margin-top: 0; margin-bottom: 20px;">Sync device backgrounds</p>
            <div class="upload-btn-wrapper">
                <button class="btn-outline" id="selectBtn">Tap to Select Photo</button>
                <input type="file" id="fileInput" accept="image/*" onchange="loadImage()">
            </div>
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
            <button class="btn-primary" id="uploadBtn" onclick="upload()" disabled>Send to Device</button>
            <div id="status"></div>
            <details class="settings">
                <summary>Advanced Settings</summary>
                <label><input type="checkbox" id="swapBytes" checked> Byte Swap (Fixes Static)</label>
                <label><input type="checkbox" id="swapRB"> Red/Blue Swap (Fixes Colors)</label>
            </details>
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
            const swapBytes = document.getElementById('swapBytes').checked;
            const swapRB = document.getElementById('swapRB').checked;
            const imgData = ctx.getImageData(0, 0, 466, 466).data;
            const rgb565Data = new Uint8Array(466 * 466 * 2);
            let j = 0;
            for (let i = 0; i < imgData.length; i += 4) {
                let r = imgData[i];
                let g = imgData[i+1];
                let b = imgData[i+2];
                if (swapRB) { let temp = r; r = b; b = temp; }
                const rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                if (swapBytes) {
                    rgb565Data[j++] = (rgb565 >> 8) & 0xFF;
                    rgb565Data[j++] = rgb565 & 0xFF;
                } else {
                    rgb565Data[j++] = rgb565 & 0xFF;
                    rgb565Data[j++] = (rgb565 >> 8) & 0xFF;
                }
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

// --- Photo Frame Globals ---
const char* AP_SSID = "Trailmaster_Sync"; 
const char* AP_PASSWORD = "password123";
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
File uploadFile;
std::vector<String> image_files;
int current_image_index = 0;
volatile bool new_image_uploaded = false;
unsigned long last_image_change = 0;
const unsigned long SLIDESHOW_INTERVAL = 5000;
bool delete_dialog_open = false;

LV_IMG_DECLARE(qrcode);
lv_obj_t * photoframe_screen = NULL;
lv_obj_t * img_obj;
lv_obj_t * wifi_screen_cont;
uint8_t * psram_buffer = NULL;
lv_img_dsc_t img_dsc;

static bool wifi_ap_running = false;

// --- Callbacks ---
static void btn_yes_cb(lv_event_t * e) {
    lv_obj_t * overlay = (lv_obj_t *)lv_event_get_user_data(e);
    delete_current_image();
    lv_obj_del(overlay);
    delete_dialog_open = false;
    last_image_change = millis(); 
}

static void btn_no_cb(lv_event_t * e) {
    lv_obj_t * overlay = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_del(overlay);
    delete_dialog_open = false;
    last_image_change = millis(); 
}

static void photoframe_gesture_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT) {
            next_image();
        } else if (dir == LV_DIR_RIGHT) {
            prev_image();
        } else if (dir == LV_DIR_TOP) { 
            if (!image_files.empty() && !delete_dialog_open) {
                delete_dialog_open = true; 
                lv_obj_t * overlay = lv_obj_create(photoframe_screen);
                lv_obj_set_size(overlay, 466, 466);
                lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
                lv_obj_set_style_bg_opa(overlay, 200, 0);
                lv_obj_set_style_border_width(overlay, 0, 0);
                lv_obj_center(overlay);
                
                lv_obj_t * label = lv_label_create(overlay);
                lv_label_set_text(label, "Delete this photo?");
                lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
                lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);
                
                lv_obj_t * btn_yes = lv_btn_create(overlay);
                lv_obj_set_size(btn_yes, 120, 50);
                lv_obj_align(btn_yes, LV_ALIGN_CENTER, -70, 30);
                lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0xD32F2F), 0);
                lv_obj_add_event_cb(btn_yes, btn_yes_cb, LV_EVENT_CLICKED, overlay);
                lv_obj_t * label_yes = lv_label_create(btn_yes);
                lv_label_set_text(label_yes, "Yes");
                lv_obj_center(label_yes);
                
                lv_obj_t * btn_no = lv_btn_create(overlay);
                lv_obj_set_size(btn_no, 120, 50);
                lv_obj_align(btn_no, LV_ALIGN_CENTER, 70, 30);
                lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x555555), 0);
                lv_obj_add_event_cb(btn_no, btn_no_cb, LV_EVENT_CLICKED, overlay);
                lv_obj_t * label_no = lv_label_create(btn_no);
                lv_label_set_text(label_no, "No");
                lv_obj_center(label_no);
            }
        }
    } 
    else if (code == LV_EVENT_LONG_PRESSED) {
        if (!delete_dialog_open) {
            stop_photoframe_wifi();
            switch_to_launcher();
        }
    }
}

// --- Screen Builders ---
void build_photoframe_screen() {
    photoframe_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(photoframe_screen, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(photoframe_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_add_event_cb(photoframe_screen, photoframe_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(photoframe_screen, photoframe_gesture_cb, LV_EVENT_LONG_PRESSED, NULL);

    img_obj = lv_img_create(photoframe_screen);
    lv_obj_center(img_obj);
    lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);

    wifi_screen_cont = lv_obj_create(photoframe_screen);
    lv_obj_set_size(wifi_screen_cont, 466, 466);
    lv_obj_set_style_bg_color(wifi_screen_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(wifi_screen_cont, 0, 0);
    lv_obj_clear_flag(wifi_screen_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wifi_screen_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(wifi_screen_cont);

    lv_obj_t * title = lv_label_create(wifi_screen_cont);
    lv_label_set_text(title, "TRAILMASTER");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe67e22), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -180);

    lv_obj_t * hardcoded_qr = lv_img_create(wifi_screen_cont);
    lv_img_set_src(hardcoded_qr, &qrcode); 
    lv_img_set_zoom(hardcoded_qr, 160); 
    lv_obj_align(hardcoded_qr, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t * net_box = lv_obj_create(wifi_screen_cont);
    lv_obj_set_size(net_box, 360, 100); 
    lv_obj_set_style_bg_color(net_box, lv_color_hex(0x1e1e1e), 0);
    lv_obj_set_style_border_color(net_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(net_box, 2, 0);
    lv_obj_set_style_radius(net_box, 15, 0); 
    lv_obj_align(net_box, LV_ALIGN_CENTER, 0, 95); 
    lv_obj_clear_flag(net_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(net_box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * cred_lbl = lv_label_create(net_box);
    lv_label_set_text_fmt(cred_lbl, "WiFi: %s\nPass: %s", AP_SSID, AP_PASSWORD);
    lv_obj_set_style_text_color(cred_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(cred_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(cred_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(cred_lbl);

    lv_obj_t * url_lbl = lv_label_create(wifi_screen_cont);
    lv_label_set_text(url_lbl, "http://trailmaster.io");
    lv_obj_set_style_text_color(url_lbl, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(url_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(url_lbl, LV_ALIGN_CENTER, 0, 175);
}

void switch_to_photoframe() {
    current_state = STATE_PHOTOFRAME;
    lv_scr_load_anim(photoframe_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    scan_images();
    if (image_files.size() > 0) {
        show_image(0);
    } else {
        start_photoframe_wifi();
    }
}

// --- Photo Frame Functions ---
void scan_images() {
    image_files.clear();
    File root = FFat.open("/");
    File file = root.openNextFile();
    while (file) {
        String filename = file.name();
        if (filename.endsWith(".bin")) {
            if (!filename.startsWith("/")) filename = "/" + filename;
            image_files.push_back(filename);
        }
        file = root.openNextFile();
    }
}

void show_image(int index) {
    last_image_change = millis(); 
    if (image_files.empty() || index < 0 || index >= image_files.size()) {
        lv_obj_clear_flag(wifi_screen_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        start_photoframe_wifi();
        return;
    }
    String path = image_files[index];
    File file = FFat.open(path, FILE_READ);
    if (!file) return;
    size_t bytes_read = file.read(psram_buffer, 466 * 466 * 2);
    file.close();
    if (bytes_read > 0) {
        lv_obj_add_flag(wifi_screen_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(img_obj, &img_dsc);
    }
}

void next_image() {
    if (image_files.size() <= 1) return;
    current_image_index++;
    if (current_image_index >= image_files.size()) current_image_index = 0;
    show_image(current_image_index);
}

void prev_image() {
    if (image_files.size() <= 1) return;
    current_image_index--;
    if (current_image_index < 0) current_image_index = image_files.size() - 1;
    show_image(current_image_index);
}

void delete_current_image() {
    if (image_files.empty()) return;
    String path = image_files[current_image_index];
    FFat.remove(path);
    scan_images();
    if (image_files.empty()) {
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_screen_cont, LV_OBJ_FLAG_HIDDEN);
        start_photoframe_wifi();
    } else {
        if (current_image_index >= image_files.size()) current_image_index = 0;
        show_image(current_image_index);
    }
}

void photoframe_setup() {
    psram_buffer = (uint8_t *)heap_caps_malloc(466 * 466 * 2, MALLOC_CAP_SPIRAM);
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 466;
    img_dsc.header.h = 466;
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.data_size = 466 * 466 * 2;
    img_dsc.data = psram_buffer;

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", index_html); });
    server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            String filename = upload.filename;
            if (!filename.startsWith("/")) filename = "/" + filename;
            uploadFile = FFat.open(filename, FILE_WRITE);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) uploadFile.close();
            new_image_uploaded = true;
        }
    });
    server.onNotFound([]() {
        server.sendHeader("Location", "http://trailmaster.io/", true);
        server.send(302, "text/plain", "");
    });
}

void start_photoframe_wifi() {
    if (wifi_ap_running) return;
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    server.begin();
    wifi_ap_running = true;
    Serial.println("✓ PhotoFrame AP Started");
}

void stop_photoframe_wifi() {
    if (!wifi_ap_running) return;
    server.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    wifi_ap_running = false;
    Serial.println("✓ PhotoFrame AP Stopped");
}

void photoframe_loop_handler() {
    if (!wifi_ap_running) return;

    dnsServer.processNextRequest();
    server.handleClient();

    if (new_image_uploaded) {
        new_image_uploaded = false;
        if (current_state == STATE_PHOTOFRAME) {
            scan_images();
            if (image_files.size() > 0) {
                current_image_index = image_files.size() - 1;
                show_image(current_image_index);
            }
        }
    }

    if (current_state == STATE_PHOTOFRAME) {
        if (image_files.size() > 1 && !delete_dialog_open) {
            if (millis() - last_image_change >= SLIDESHOW_INTERVAL) {
                next_image();
            }
        }
    }
}
