# -*- coding: utf-8 -*-
# 生成 16x16 中文字模 (C 数组)
# 字体 simhei 16px, 格式: 阴码 / 逐行 / 高位在前(左) / 16进制
# 索引顺序: 郑0 州1 轻2 工3 业4 大5 学6 前7 进8 后9 退10 停11 止12 左13 转14 右15 张16 毅17 世18 昌19 朱20 志21 鹏22
from PIL import Image, ImageDraw, ImageFont

chars = "郑州轻工业大学前进后退停止左转右张毅世昌朱志鹏"
FONT_PATH = "C:/Windows/Fonts/simhei.ttf"

font = ImageFont.truetype(FONT_PATH, 24)  # 大字号渲染后缩放，保证笔画完整

lines = []
lines.append("// 自动生成: 字体 simhei 24px 缩放至 16x16, 阴码逐行高位在前")
lines.append("// 索引: 郑0 州1 轻2 工3 业4 大5 学6 前7 进8 后9 退10 停11 止12 左13 转14 右15 张16 毅17 世18 昌19 朱20 志21 鹏22")
lines.append("static const uint8_t HZ_LIB[23][32] = {")
for ch in chars:
    # 先画到大图再缩放到 16x16
    big = Image.new("L", (32, 32), 0)
    d = ImageDraw.Draw(big)
    d.text((16, 16), ch, fill=255, font=font, anchor="mm")
    small = big.resize((16, 16))
    px = small.load()
    bl = []
    for y in range(16):
        val = 0
        for x in range(16):
            if px[x, y] > 128:
                val |= (1 << (15 - x))
        bl.append(val >> 8)
        bl.append(val & 0xFF)
    hexs = ", ".join("0x%02X" % b for b in bl)
    lines.append("    { %s }, // %s" % (hexs, ch))
lines.append("};")

out = "\n".join(lines)
with open("hz_lib.txt", "w", encoding="utf-8") as f:
    f.write(out)
print("OK, %d chars generated -> hz_lib.txt" % len(chars))
