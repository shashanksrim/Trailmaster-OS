const { Resvg } = require('@resvg/resvg-js');
const fs = require('fs');

const icons = {
    "gauges": {
        "color": "#4ade80",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" xmlns="http://www.w3.org/2000/svg"><path d="M12 14a2 2 0 100-4 2 2 0 000 4z"/><path d="M12 4a8 8 0 018 8H4a8 8 0 018-8z"/><path d="M12 14l-3-3"/></svg>'
    },
    "speed": {
        "color": "#f97316",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="9"/><path d="M12 12l-4-4"/><path d="M7.5 7.5L12 12"/><circle cx="12" cy="12" r="1.5" fill="{color}"/></svg>'
    },
    "incline": {
        "color": "#38bdf8",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="9"/><line x1="3" y1="12" x2="21" y2="12"/><circle cx="12" cy="12" r="2" fill="{color}" stroke="none"/><path d="M12 3v2M12 19v2"/></svg>'
    },
    "image": {
        "color": "#f97316",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" xmlns="http://www.w3.org/2000/svg"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>'
    },
    "game": {
        "color": "#38bdf8",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" xmlns="http://www.w3.org/2000/svg"><rect x="2" y="6" width="20" height="12" rx="2" ry="2"></rect><line x1="6" y1="12" x2="10" y2="12"></line><line x1="8" y1="10" x2="8" y2="14"></line><line x1="15" y1="13" x2="15.01" y2="13"></line><line x1="18" y1="11" x2="18.01" y2="11"></line></svg>'
    },
    "settings": {
        "color": "#5a7060",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" xmlns="http://www.w3.org/2000/svg"><path d="M12 15a3 3 0 100-6 3 3 0 000 6z"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/></svg>'
    },
    "about": {
        "color": "#f87171",
        "svg": '<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.5" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>'
    }
};

function generateCArray(name, pixels, width, height) {
    let data = [];
    for (let i = 0; i < pixels.length; i += 4) {
        let r = pixels[i];
        let g = pixels[i+1];
        let b = pixels[i+2];
        let a = pixels[i+3];
        
        let c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        let hb = (c565 >> 8) & 0xFF;
        let lb = c565 & 0xFF;
        data.push(lb, hb, a);
    }
    
    let lines = [];
    lines.push(`#include "lvgl.h"`);
    lines.push(`const uint8_t ui_img_icon_${name}_map[] = {`);
    
    for (let i = 0; i < data.length; i += 12) {
        let chunk = data.slice(i, i+12);
        lines.push("  " + chunk.map(x => "0x" + x.toString(16).padStart(2, '0')).join(", ") + ",");
    }
    lines.push(`};`);
    lines.push(`const lv_img_dsc_t ui_img_icon_${name} = {`);
    lines.push(`  .header.always_zero = 0,`);
    lines.push(`  .header.w = ${width},`);
    lines.push(`  .header.h = ${height},`);
    lines.push(`  .data_size = ${data.length},`);
    lines.push(`  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,`);
    lines.push(`  .data = ui_img_icon_${name}_map,`);
    lines.push(`};`);
    
    fs.writeFileSync(`ui_img_icon_${name}.c`, lines.join('\n'));
}

for (const [name, icon] of Object.entries(icons)) {
    const svgStr = icon.svg.replace(/{color}/g, icon.color);
    const resvg = new Resvg(svgStr, { background: 'rgba(0,0,0,0)' });
    const pngData = resvg.render();
    const image = pngData.asPng();
    const pixels = pngData.pixels;
    generateCArray(name, pixels, pngData.width, pngData.height);
    console.log(`Generated ${name}`);
}
