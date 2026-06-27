from PIL import Image
for file in ["scratch/trailmaster.bmp", "scratch/trailmaster_jap.bmp"]:
    img = Image.open(file).convert("RGBA")
    has_alpha = any(a < 255 for _, _, _, a in img.getdata())
    print(f"{file} has alpha: {has_alpha}")
