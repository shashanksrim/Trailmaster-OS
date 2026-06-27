from PIL import Image
import os

# Find the generated image (it's the latest png in the artifact dir)
artifact_dir = "/Users/sriramshashank/.gemini/antigravity/brain/fc410260-4d43-4cc0-8894-7196504bd808"
files = [f for f in os.listdir(artifact_dir) if f.endswith('.png') and 'rhino_pixel_art' in f]
files.sort(key=lambda x: os.path.getmtime(os.path.join(artifact_dir, x)), reverse=True)
latest_img = os.path.join(artifact_dir, files[0])

# Open and resize
img = Image.open(latest_img).convert('L')
img = img.resize((40, 35), Image.Resampling.NEAREST)

# Threshold
threshold = 128
pixels = []
for y in range(35):
    for x in range(40):
        # Black (0) is rhino, White (255) is background
        # We want 1 for Rhino, 0 for background
        v = img.getpixel((x, y))
        if v < threshold:
            pixels.append(1)
        else:
            pixels.append(0)

# Pack into XBM (LSB first)
packed = []
for y in range(35):
    row_bytes = []
    for i in range(0, 40, 8):
        byte = 0
        for bit in range(8):
            if pixels[y*40 + i + bit]:
                byte |= (1 << bit)
        row_bytes.append(f"0x{byte:02X}")
    packed.append(row_bytes)

print("static const uint8_t rhino_bits[3][200] = {")
print("// Frame 0")
print("{")
for r in packed:
    print("  " + ", ".join(r) + ",")
# Pad to 200
for _ in range(35, 40):
    print("  0x00, 0x00, 0x00, 0x00, 0x00,")
print("},")

# Frame 1 & 2: Modified legs
def pack_frame(pxs):
    res = []
    for y in range(35):
        row = []
        for i in range(0, 40, 8):
            byte = 0
            for bit in range(8):
                if pxs[y*40 + i + bit]:
                    byte |= (1 << bit)
            row.append(f"0x{byte:02X}")
        res.append(row)
    return res

# Walk 1
px1 = list(pixels)
for y in range(30, 35):
    for x in range(40):
        if x < 20: px1[y*40 + x] = 0
packed1 = pack_frame(px1)
print("// Frame 1")
print("{")
for r in packed1:
    print("  " + ", ".join(r) + ",")
for _ in range(35, 40):
    print("  0x00, 0x00, 0x00, 0x00, 0x00,")
print("},")

# Walk 2
px2 = list(pixels)
for y in range(30, 35):
    for x in range(40):
        if x >= 20: px2[y*40 + x] = 0
packed2 = pack_frame(px2)
print("// Frame 2")
print("{")
for r in packed2:
    print("  " + ", ".join(r) + ",")
for _ in range(35, 40):
    print("  0x00, 0x00, 0x00, 0x00, 0x00,")
print("}")
print("};")
