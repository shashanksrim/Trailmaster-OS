import re

def count_and_pad(data_str):
    hex_vals = re.findall(r'0x[0-9a-fA-F]+', data_str)
    count = len(hex_vals)
    # Pad to 200
    while len(hex_vals) < 200:
        hex_vals.append("0x00")
    # Format nicely
    lines = []
    for i in range(0, 200, 5):
        lines.append("  " + ", ".join(hex_vals[i:i+5]) + ",")
    return "\n".join(lines)

# Read current screen_game.cpp to get the bits I just put there
with open('screen_game.cpp', 'r') as f:
    content = f.read()

# Extract Frame 0, 1, 2
frames = re.findall(r'\{(.*?)\}', content, re.DOTALL)
# The first 3 are the rhino frames
f0 = count_and_pad(frames[0])
f1 = count_and_pad(frames[1])
f2 = count_and_pad(frames[2])

print("static const uint8_t rhino_bits[3][200] = {")
print("// Frame 0")
print("{\n" + f0 + "\n},")
print("// Frame 1")
print("{\n" + f1 + "\n},")
print("// Frame 2")
print("{\n" + f2 + "\n}")
print("};")
