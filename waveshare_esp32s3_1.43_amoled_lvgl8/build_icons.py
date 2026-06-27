import os
import subprocess
from PIL import Image

icons = {
    "icon_gauges": ('#4ade80', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M12 14a2 2 0 100-4 2 2 0 000 4z"/><path d="M12 4a8 8 0 018 8H4a8 8 0 018-8z"/><path d="M12 14l-3-3"/></svg>'),
    "icon_speedo": ('#FF0000', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><path d="M12 12l-4-4"/><path d="M7.5 7.5L12 12"/><circle cx="12" cy="12" r="1.5" fill="currentColor"/></svg>'),
    "icon_incline": ('#FF0000', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><line x1="3" y1="12" x2="21" y2="12"/><circle cx="12" cy="12" r="2" fill="currentColor" stroke="none"/><path d="M12 3v2M12 19v2"/></svg>'),
    "icon_altitude": ('#f97316', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M3 18l5-8 4 5 3-4 6 7H3z"/></svg>'),
    "icon_compass": ('#4ade80', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><path d="M12 3v2M12 19v2M3 12h2M19 12h2"/><path d="M15 9l-3 3-2-2" stroke-linecap="round"/></svg>'),
    "icon_engine": ('#f87171', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="8" width="14" height="8" rx="1"/><path d="M17 10h2l2 2-2 2h-2"/><path d="M7 8V6M11 8V6"/><path d="M3 12H1"/></svg>'),
    "icon_gps": ('#38bdf8', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M12 2C8.13 2 5 5.13 5 9c0 5.25 7 13 7 13s7-7.75 7-13c0-3.87-3.13-7-7-7z"/><circle cx="12" cy="9" r="2.5"/></svg>'),
    "icon_tpms": ('#4ade80', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3"/></svg>'),
    "icon_image": ('#a78bfa', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="M21 15l-5-5L5 21"/></svg>'),
    "icon_settings": ('#5a7060', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M12.22 2h-.44a2 2 0 00-2 2v.18a2 2 0 01-1 1.73l-.43.25a2 2 0 01-2 0l-.15-.08a2 2 0 00-2.73.73l-.22.38a2 2 0 00.73 2.73l.15.1a2 2 0 011 1.72v.51a2 2 0 01-1 1.74l-.15.09a2 2 0 00-.73 2.73l.22.38a2 2 0 002.73.73l.15-.08a2 2 0 012 0l.43.25a2 2 0 011 1.73V20a2 2 0 002 2h.44a2 2 0 002-2v-.18a2 2 0 011-1.73l.43-.25a2 2 0 012 0l.15.08a2 2 0 002.73-.73l.22-.39a2 2 0 00-.73-2.73l-.15-.08a2 2 0 01-1-1.74v-.5a2 2 0 011-1.74l.15-.1a2 2 0 00.73-2.73l-.22-.38a2 2 0 00-2.73-.73l-.15.08a2 2 0 01-2 0l-.43-.25a2 2 0 01-1-1.73V4a2 2 0 00-2-2z"/><circle cx="12" cy="12" r="3"/></svg>'),
    "icon_games": ('#f87171', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="6" width="18" height="12" rx="3"/><path d="M9 12H7M8 11v2M15 11.5h.01M17 12.5h.01" stroke-linecap="round"/></svg>'),
    "icon_about": ('#38bdf8', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="9"/><path d="M12 17v-5.5M12 8V7" stroke-linecap="round"/></svg>')
}

out_c = open("grid_icons.c", "w")
out_c.write("#include <lvgl.h>\n\n")

for name, (color, svg) in icons.items():
    svg = svg.replace('currentColor', color)
    with open("tmp.svg", "w") as f:
        f.write(svg)
    # sips resizes nicely
    subprocess.run(["sips", "-z", "48", "48", "-s", "format", "png", "tmp.svg", "--out", "tmp.png"], check=True, capture_output=True)
    
    img = Image.open("tmp.png").convert("RGBA")
    pixels = img.load()
    w, h = img.size
    
    # Convert to LVGL CF_TRUE_COLOR_ALPHA (RGB565 + Alpha byte)
    out_c.write(f"const uint8_t {name}_map[] = {{\n")
    for y in range(h):
        out_c.write("    ")
        for x in range(w):
            r, g, b, a = pixels[x, y]
            # RGB565
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            # LVGL expects Little Endian (low byte, high byte) for RGB565, then Alpha byte
            out_c.write(f"0x{c & 0xFF:02X}, 0x{(c >> 8) & 0xFF:02X}, 0x{a:02X}, ")
        out_c.write("\n")
    out_c.write("};\n\n")
    
    out_c.write(f"""
const lv_img_dsc_t {name} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {w * h * 3},
  .data = {name}_map,
}};
""")

out_c.close()
print("Done!")
