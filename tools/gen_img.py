# -*- coding: utf-8 -*-
# 图片转 RGB565 C 数组 (128x160, 用于 ST7735 全屏显示)
# 输入: 任意图片文件 (PNG/JPG)
# 输出: lcd_img.h (const uint16_t 数组)
from PIL import Image
import sys, os

def rgb_to_565(r, g, b):
    """RGB888 -> RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert_image(src_path, out_path, w=128, h=160):
    img = Image.open(src_path).convert("RGB")
    img = img.resize((w, h), resample=Image.LANCZOS)
    pixels = img.load()

    lines = []
    lines.append("// 自动生成: %s -> %dx%d RGB565" % (os.path.basename(src_path), w, h))
    lines.append("#ifndef _LCD_IMG_H_")
    lines.append("#define _LCD_IMG_H_")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define IMG_W  %d" % w)
    lines.append("#define IMG_H  %d" % h)
    lines.append("static const uint16_t g_lcd_img[%d] = {" % (w * h))

    for y in range(h):
        row_vals = []
        for x in range(w):
            r, g, b = pixels[x, y]
            row_vals.append("0x%04X" % rgb_to_565(r, g, b))
        lines.append("    " + ",".join(row_vals) + ",")
    
    lines.append("};")
    lines.append("#endif")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    size_kb = (w * h * 2) / 1024
    print("OK -> %s (%d bytes = %.1f KB)" % (out_path, w*h*2, size_kb))

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python gen_img.py <image.png> [output_path]")
        sys.exit(1)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "User", "API", "lcd_img.h"
    )
    dst = os.path.normpath(dst)
    convert_image(src, dst)
