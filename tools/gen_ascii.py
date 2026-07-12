# -*- coding: utf-8 -*-
# 生成 ASCII 8x16 字模 (C 数组)
# 阴码 / 逐行 / 高位在前 / 16进制, 宽8高16
#
# 说明: 之前用 simhei 大字号缩放, 句号(.)冒号(:)等小标点被抗锯齿平均掉变成空白。
# 这里改用等宽拉丁字体 Courier New, 直接按 8x16 目标尺寸渲染(基线锚点), 保证标点可见。
from PIL import Image, ImageDraw, ImageFont

chars = [chr(c) for c in range(0x20, 0x7F)]  # 32..126 共95个字符

# 优先用 Courier New (等宽, 小字号标点清晰); 缺失则回退系统默认
import os
FONT_PATH = "C:/Windows/Fonts/cour.ttf"
if not os.path.exists(FONT_PATH):
    FONT_PATH = "C:/Windows/Fonts/arial.ttf"
font = ImageFont.truetype(FONT_PATH, 13)

lines = []
lines.append("// 自动生成: ASCII 8x16 (cour.ttf), 阴码逐行高位在前")
lines.append("static const uint8_t ASCII_LIB[95][16] = {")
for ch in chars:
    img = Image.new("L", (8, 16), 0)
    d = ImageDraw.Draw(img)
    # 基线锚点: 左对齐(x=0), 基线 y=13, 让字母/标点落位自然
    d.text((0, 13), ch, fill=255, font=font, anchor="ls")
    px = img.load()
    bl = []
    for y in range(16):
        val = 0
        for x in range(8):
            if px[x, y] > 110:
                val |= (1 << (7 - x))
        bl.append(val)
    hexs = ", ".join("0x%02X" % b for b in bl)
    lines.append("    { %s }," % hexs)
lines.append("};")

with open("ascii_lib.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print("OK ascii %d chars -> ascii_lib.txt" % len(chars))
