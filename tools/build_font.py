# -*- coding: utf-8 -*-
# 合并 hz_lib.txt 与 ascii_lib.txt 为 User/API/lcd_font.h
import os

base = os.path.dirname(os.path.abspath(__file__))

with open(os.path.join(base, "hz_lib.txt"), encoding="utf-8") as f:
    hz = f.read().strip()
with open(os.path.join(base, "ascii_lib.txt"), encoding="utf-8") as f:
    asc = f.read().strip()

out = []
out.append("// ============================================================")
out.append("// 自动生成字模 (tools/gen_hzk.py + tools/gen_ascii.py)")
out.append("// 字体: simhei.ttf(中文16x16) / cour.ttf(ASCII 8x16), 阴码 / 逐行 / 高位在前")
out.append("// 请勿手改，需要增删汉字请改脚本重新生成")
out.append("// ============================================================")
out.append("")
out.append(hz)
out.append("")
out.append(asc)
out.append("")

dst = os.path.normpath(os.path.join(base, "..", "User", "API", "lcd_font.h"))
with open(dst, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
print("OK -> %s" % dst)
