import os
from PIL import Image

src_img_path = "/Users/sriramshashank/Documents/GitHub/hellojimny/waveshare_esp32s3_1.43_amoled_lvgl8/scratch/trailmaster.bmp"
dst_c_path = "/Users/sriramshashank/Documents/GitHub/hellojimny/waveshare_esp32s3_1.43_amoled_lvgl8/ui_img_trailmaster.c"

def convert_image():
    if not os.path.exists(src_img_path):
        print(f"Source image not found: {src_img_path}")
        return

    # Open image and convert to RGBA
    img = Image.open(src_img_path).convert('RGBA')
    # Resize it natively to be 12% smaller than the 0.864 scale (0.864 * 0.88 = ~0.76)
    W = int(img.width * 0.76)
    H = int(img.height * 0.76)
    img = img.resize((W, H), Image.Resampling.LANCZOS)

    c_bytes = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = img.getpixel((x, y))
            
            # We rely on the native alpha channel of the BMP rather than punching holes
            
            # Convert to RGB565
            r5 = (r * 31) // 255
            g6 = (g * 63) // 255
            b5 = (b * 31) // 255
            
            # Pack as 16-bit word (no swap):
            # LVGL little endian puts low-byte first, then high-byte
            val = (r5 << 11) | (g6 << 5) | b5
            low_byte = val & 0xFF
            high_byte = (val >> 8) & 0xFF
            
            c_bytes.append(low_byte)
            c_bytes.append(high_byte)
            c_bytes.append(a)  # Alpha byte

    # Write out the C-file
    with open(dst_c_path, 'w') as f:
        f.write("// This file is custom converted for LVGL v8\n")
        f.write("#include \"ui.h\"\n\n")
        f.write("#ifndef LV_ATTRIBUTE_MEM_ALIGN\n")
        f.write("#define LV_ATTRIBUTE_MEM_ALIGN\n")
        f.write("#endif\n\n")
        f.write(f"// IMAGE DATA: Trailmaster Logo ({W}x{H} px)\n")
        f.write(f"const LV_ATTRIBUTE_MEM_ALIGN uint8_t ui_img_trailmaster_data[] = {{\n")
        
        # Write bytes in rows of 16 for clean formatting
        line_bytes = []
        for i, byte in enumerate(c_bytes):
            line_bytes.append(f"0x{byte:02X}")
            if len(line_bytes) == 16:
                f.write("    " + ",".join(line_bytes) + ",\n")
                line_bytes = []
        if line_bytes:
            f.write("    " + ",".join(line_bytes) + "\n")
            
        f.write("};\n\n")
        f.write("const lv_img_dsc_t ui_img_trailmaster = {\n")
        f.write("    .header.always_zero = 0,\n")
        f.write(f"    .header.w = {W},\n")
        f.write(f"    .header.h = {H},\n")
        f.write("    .data_size = sizeof(ui_img_trailmaster_data),\n")
        f.write("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n")
        f.write("    .data = ui_img_trailmaster_data\n")
        f.write("};\n")

    print(f"Successfully converted PNG to C-array at {dst_c_path}")

if __name__ == "__main__":
    convert_image()
