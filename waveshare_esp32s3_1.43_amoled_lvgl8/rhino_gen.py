def print_xbm(name, rows):
    print(f"static const uint8_t {name}[] = {{")
    for row in rows:
        row = row.ljust(40, ' ')
        bytes_out = []
        for i in range(0, 40, 8):
            byte_str = row[i:i+8]
            byte_val = 0
            for bit_idx, char in enumerate(byte_str):
                if char != ' ':
                    byte_val |= (1 << bit_idx)
            bytes_out.append(f"0x{byte_val:02X}")
        print("  " + ",".join(bytes_out) + ",")
    print("};")

# 40x35
# Rhino: Thick neck, head attached, horns on top of snout.
rhino_common = [
    "                                        ", # 0
    "                                        ",
    "                                        ",
    "                                        ",
    "                                        ",
    "                                        ", # 5
    "                                        ",
    "                                        ",
    "                                        ",
    "                                        ",
    "                      XXXX              ", # 10 (Ear)
    "                     XXXXX              ",
    "      XXXXXXXXXXXXXXXXXXXX              ", # 12 (Top of back)
    "     XXXXXXXXXXXXXXXXXXXXXX             ",
    "    XXXXXXXXXXXXXXXXXXXXXXX             ", # 14
    "   XXXXXXXXXXXXXXXXXXXXXXXXX            ",
    "  XXXXXXXXXXXXXXXXXXXXXXXXXXX      X    ", # 16 (Horn 1)
    " XXXXXXXXXXXXXXXXXXXXXXXXXXXX     XXX   ", # 17 (Horn 1)
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXX    XXXXX  ", # 18 (Horn 2 base)
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX  XXXXXX  ", # 19
    "XXXXXXXXXXXXXXXXXXXXX  XXXXXXX XXXXXXX  ", # 20 (EYE at col 21-22)
    "XXXXXXXXXXXXXXXXXXXXX  XXXXXXXXXXXXXXXX ", # 21 (EYE + Bridge to head)
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX ", # 22 (Full bulky head)
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX  ", # 23
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX    ", # 24
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX          ", # 25
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXX           ", # 26
    " XXXXXXXXXXXXXXXXXXXXXXXXXX             ", # 27
    "  XXXXXXXXXXXXXXXXXXXXXXXX              ", # 28
    "   XXXXXXX        XXXXXXX               ", # 29 (Legs)
    "   XXXXXXX        XXXXXXX               ", # 30
    "   XXXXXXX        XXXXXXX               ", # 31
    "   XXXXXXX        XXXXXXX               ", # 32
    "  XXXXXXXXX      XXXXXXXXX              ", # 33
    "                                        ", # 34
]

rhino_frame0 = list(rhino_common)

rhino_frame1 = list(rhino_common) # Left step
for i in range(29, 34):
    rhino_frame1[i] = "   XXX            XXXXXXX               "
    if i > 31: rhino_frame1[i] = "   XXXXXXX        XXXXXXX               "

rhino_frame2 = list(rhino_common) # Right step
for i in range(29, 34):
    rhino_frame2[i] = "   XXXXXXX            XXX               "
    if i > 31: rhino_frame2[i] = "   XXXXXXX        XXXXXXX               "

print("// Frame 0")
print_xbm("rhino_f0", rhino_frame0)
print("// Frame 1")
print_xbm("rhino_f1", rhino_frame1)
print("// Frame 2")
print_xbm("rhino_f2", rhino_frame2)
