# -*- coding: utf-8 -*-
# 动图(多帧)转 RGB565 C 数组 (128x160, 用于 ST7735 全屏动画播放)
# 输入: 图片目录(多张 PNG/JPG, 按文件名数字排序) 或 单个 GIF
# 输出: lcd_anim.h (const uint16_t g_lcd_frames[FRAME_COUNT][W*H])
#
# 用法:
#   python gen_frames.py <图片目录或gif> [输出路径]
# 例:
#   python gen_frames.py "D:\STM32_Dev_Env\05.Documents\动图抽帧" ../User/API/lcd_anim.h
from PIL import Image, ImageSequence
import sys, os, glob

W, H = 128, 160
MAX_FRAMES = 12   # 上限保护, Flash 256KB 装不下太多帧(每帧40KB)

def rgb_to_565(r, g, b):
    """RGB888 -> RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def _num_key(path):
    # 取文件名中的数字部分用于排序(1,2,10 而非 1,10,2)
    digits = ''.join(ch for ch in os.path.basename(path) if ch.isdigit())
    return int(digits) if digits else 0

def load_frames(src):
    frames = []
    if os.path.isdir(src):
        files = glob.glob(os.path.join(src, "*"))
        files = [f for f in files if f.lower().endswith(
            ('.png', '.jpg', '.jpeg', '.bmp', '.gif'))]
        files.sort(key=_num_key)
        for f in files:
            img = Image.open(f).convert("RGB")
            frames.append(img.resize((W, H), Image.LANCZOS))
    elif src.lower().endswith('.gif'):
        with Image.open(src) as im:
            for frame in ImageSequence.Iterator(im):
                frames.append(frame.convert("RGB").resize((W, H), Image.LANCZOS))
    else:
        img = Image.open(src).convert("RGB")
        frames.append(img.resize((W, H), Image.LANCZOS))
    return frames

def convert(src, out_path):
    frames = load_frames(src)
    if not frames:
        print("ERROR: 没有读到任何帧")
        sys.exit(1)
    if len(frames) > MAX_FRAMES:
        print("WARN: 帧数 %d 超过上限 %d, 只取前 %d 帧"
              % (len(frames), MAX_FRAMES, MAX_FRAMES))
        frames = frames[:MAX_FRAMES]
    n = len(frames)

    lines = []
    lines.append("// 自动生成(动图): %s -> %dx%d RGB565, %d 帧"
                 % (os.path.basename(src), W, H, n))
    lines.append("#ifndef _LCD_ANIM_H_")
    lines.append("#define _LCD_ANIM_H_")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define ANIM_W  %d" % W)
    lines.append("#define ANIM_H  %d" % H)
    lines.append("#define FRAME_COUNT  %d" % n)
    lines.append("static const uint16_t g_lcd_frames[%d][%d] = {"
                 % (n, W * H))
    for fi, img in enumerate(frames):
        pixels = img.load()
        lines.append("  // frame %d" % fi)
        for y in range(H):
            row = []
            for x in range(W):
                r, g, b = pixels[x, y]
                row.append("0x%04X" % rgb_to_565(r, g, b))
            lines.append("    " + ",".join(row) + ",")
    lines.append("};")
    lines.append("#endif")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    size_kb = n * W * H * 2 / 1024.0
    print("OK -> %s (%d 帧, %.1f KB)" % (out_path, n, size_kb))

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python gen_frames.py <图片目录或gif> [输出路径]")
        sys.exit(1)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "User", "API", "lcd_anim.h")
    dst = os.path.normpath(dst)
    convert(src, dst)
