import cairosvg
from PIL import Image
import io
import os

icons = {
    "gauges": {
        "color": "#4ade80",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><path d="M12 14a2 2 0 100-4 2 2 0 000 4z"/><path d="M12 4a8 8 0 018 8H4a8 8 0 018-8z"/><path d="M12 14l-3-3"/></svg>'
    },
    "speed": {
        "color": "#f97316",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><path d="M12 12l-4-4"/><path d="M7.5 7.5L12 12"/><circle cx="12" cy="12" r="1.5" fill="{color}"/></svg>'
    },
    "incline": {
        "color": "#38bdf8",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><line x1="3" y1="12" x2="21" y2="12"/><circle cx="12" cy="12" r="2" fill="{color}" stroke="none"/><path d="M12 3v2M12 19v2"/></svg>'
    },
    "image": {
        "color": "#f97316",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>'
    },
    "game": {
        "color": "#38bdf8",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><path d="M21 11.5a8.38 8.38 0 0 1-.9 3.8 8.5 8.5 0 0 1-7.6 4.7 8.38 8.38 0 0 1-3.8-.9L3 21l1.9-5.7a8.38 8.38 0 0 1-.9-3.8 8.5 8.5 0 0 1 4.7-7.6 8.38 8.38 0 0 1 3.8-.9h.5a8.48 8.48 0 0 1 8 8v.5z"/></svg>'
    },
    "settings": {
        "color": "#5a7060",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><path d="M12 15a3 3 0 100-6 3 3 0 000 6z"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/></svg>'
    },
    "about": {
        "color": "#f87171",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>'
    }
}

def generate_c_array(name, img):
    w, h = img.size
    data = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            # RGB565 format (LV_COLOR_FORMAT_NATIVE for ESP32 LVGL 8 is usually 16-bit swapped)
            # LVGL 8 with color depth 16:
            # Color is 16-bit (R5 G6 B5). 
            c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            # High byte, Low byte
            hb = (c565 >> 8) & 0xFF
            lb = c565 & 0xFF
            
            # Since ESP32 is little-endian, but LVGL usually expects native byte order depending on SWAP flag.
            # In Squareline exported projects for Waveshare, LV_COLOR_16_SWAP is often 1.
            # Let's check `lv_conf.h` if SWAP is 1. If we don't know, we can format as TRUE_COLOR_ALPHA.
            # TRUE_COLOR_ALPHA expects 3 bytes per pixel in LVGL 8 (RGB565 + Alpha). Wait, LVGL 8 true color alpha:
            # uint8_t red, green, blue, alpha; ? No, it's:
            # "For 16-bit colors, each pixel is 3 bytes: color_low, color_high, alpha"
            data.extend([lb, hb, a])
    
    lines = []
    lines.append(f"#include \"lvgl.h\"")
    lines.append(f"const uint8_t ui_img_{name}_map[] = {{")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"const lv_img_dsc_t ui_img_{name}_png = {{")
    lines.append(f"  .header.always_zero = 0,")
    lines.append(f"  .header.w = {w},")
    lines.append(f"  .header.h = {h},")
    lines.append(f"  .data_size = {len(data)},")
    lines.append(f"  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
    lines.append(f"  .data = ui_img_{name}_map,")
    lines.append(f"}};")
    
    with open(f"ui_img_{name}.c", "w") as f:
        f.write("\n".join(lines))

for name, icon in icons.items():
    svg_str = icon["svg"].format(color=icon["color"])
    png_data = cairosvg.svg2png(bytestring=svg_str.encode('utf-8'))
    img = Image.open(io.BytesIO(png_data)).convert("RGBA")
    generate_c_array(name, img)
    print(f"Generated {name}")

