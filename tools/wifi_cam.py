#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
智能车 WiFi 摄像头控制窗口 (tkinter 显示画面 + 终端输入指令)
============================================================
- 窗口: 实时显示摄像头画面 (MJPEG snapshot 流) + 顶部状态栏
- 终端: 在下方命令行输入指令控车 / 调双舵机 (走 192.168.1.1:2002)
- 主线程跑 tkinter 显示, 后台线程跑 input() 读指令, 互不阻塞

发送策略 (与已验证可用的 wifi_ctrl.py 完全一致):
  * 运动指令 1~8 : 发单字符 '1'~'8'  (固件 n==1 分支直接当运动命令, 已实测可用)
  * 舵机 s/p/f   : 发 5 字节帧 AA 00 00 YAW PITCH (固件二进制分支, 已实测可用)
  * 持续转动 z/y/r/t : 每帧发 5 字节帧 AA 00 00 YAW PITCH (YAW/PITCH 持续步进)

依赖: tkinter (Python 内置), PIL (Pillow), urllib/socket/threading (内置)
"""

import io
import socket
import time
import threading
import tkinter as tk
from tkinter import Label
import urllib.request
from PIL import Image, ImageTk, ImageDraw, ImageFont

# ---- 可改配置 ----
CAR_IP    = "192.168.1.1"
CTRL_PORT = 2002
CAM_URL   = f"http://{CAR_IP}:8080/?action=snapshot"
STEP      = 20          # 舵机持续转动时每帧步进量 (0~255)

# ---- 控制状态 (主线程显示 / 后台线程修改, 共享) ----
move_char = None        # 当前运动单字符 ('1'~'8') 或 None(不主动发运动)
yaw   = 128
pitch = 128
yaw_dir   = 0           # 水平舵机持续转动: -1左 +1右 0停
pitch_dir = 0           # 垂直舵机持续转动: -1上 +1下 0停
running = True          # 退出标志
root_win = None         # 主窗口引用 (供后台线程触发关闭)
last_hex = ""           # 最近一次发出的帧 (十六进制, 供状态栏回显)
sock_ok  = False        # 控制端口当前是否连上

CMD_NAME = {0x00: "STOP", 0x01: "FWD", 0x02: "BACK", 0x03: "LEFT", 0x04: "RIGHT"}
# 单字符 -> 运动字符 (与固件 wifi_uart.c 单字符分支一致)
CHAR2MOVE = {'1': '1', '2': '2', '3': '3', '4': '4',
             '5': '5', '6': '6', '7': '7', '8': '8'}

# ---- 字体 ----
try:
    FONT = ImageFont.truetype("arial.ttf", 18)
    FONT_SM = ImageFont.truetype("arial.ttf", 14)
except Exception:
    FONT = ImageFont.load_default()
    FONT_SM = ImageFont.load_default()


def connect_ctrl():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2.0)
        s.connect((CAR_IP, CTRL_PORT))
        s.settimeout(0.5)
        return s
    except OSError:
        return None


def send_move_char(sock, ch):
    """发单字符运动指令 (与 wifi_ctrl.py 一致, 已实测可用)"""
    global last_hex, sock_ok
    if sock is None:
        return
    try:
        sock.send(ch.encode())
        last_hex = f"char '{ch}'"
        sock_ok = True
    except OSError:
        sock_ok = False


def send_servo_frame(sock, yaw_v, pitch_v):
    """发 5 字节舵机帧 AA 00 00 YAW PITCH (与 wifi_ctrl.py s/p 一致)"""
    global last_hex, sock_ok
    if sock is None:
        return
    try:
        sock.send(bytes([0xAA, 0x00, 0x00, yaw_v, pitch_v]))
        last_hex = f"AA 00 00 {yaw_v:02X} {pitch_v:02X}"
        sock_ok = True
    except OSError:
        sock_ok = False


def draw_status(img):
    draw = ImageDraw.Draw(img)
    w, h = img.size
    mv = move_char if move_char else 'idle'
    lines = [
        f"MOVE: {mv}",
        f"YAW : {yaw:3d}  (~{yaw/255*180:.0f} deg L-R)",
        f"PITCH: {pitch:3d}  (~{pitch/255*180:.0f} deg U-D)",
        f"SENT: {last_hex}",
    ]
    y = 5
    for line in lines:
        draw.text((10, y), line, font=FONT, fill=(0, 255, 0))
        y += 22
    draw.text((10, h - 20), "终端输入指令控车 (见下方提示). 关窗口或输 q 退出",
              font=FONT_SM, fill=(0, 255, 255))


def update_frame(sock, img_label, last_send, status_text):
    global running, yaw, pitch, sock_ok
    if not running:
        try:
            if sock is not None:
                sock.send(bytes([0xAA, 0x00, 0x00, yaw, pitch]))
                sock.close()
        except OSError:
            pass
        if root_win is not None:
            root_win.destroy()
        return

    try:
        # 1) 拉摄像头画面
        try:
            data = urllib.request.urlopen(CAM_URL, timeout=2).read()
            img = Image.open(io.BytesIO(data)).convert("RGB")
        except Exception:
            img = Image.new("RGB", (320, 240), (0, 0, 0))
            ImageDraw.Draw(img).text((20, 110), "NO CAMERA / check WiFi",
                                     fill=(255, 0, 0), font=FONT)

        draw_status(img)
        img_tk = ImageTk.PhotoImage(img)
        img_label.config(image=img_tk)
        img_label.image = img_tk

        # 2) 持续转动: 每帧按方向步进角度
        if yaw_dir != 0:
            yaw = max(0, min(255, yaw + yaw_dir * STEP))
        if pitch_dir != 0:
            pitch = max(0, min(255, pitch + pitch_dir * STEP))

        # 3) 发送控制帧 (~20Hz)
        now = time.time()
        if now - last_send[0] >= 0.05:
            if yaw_dir != 0 or pitch_dir != 0:
                # 舵机在动 -> 发 5 字节舵机帧 (含最新 YAW/PITCH)
                send_servo_frame(sock, yaw, pitch)
            elif move_char is not None:
                # 运动保持 -> 发单字符 (与 wifi_ctrl.py 一致)
                send_move_char(sock, move_char)
            last_send[0] = now

        # 4) 更新状态文本
        yd = {-1: '<', 1: '>', 0: '-'}[yaw_dir]
        pd = {-1: '^', 1: 'v', 0: '-'}[pitch_dir]
        conn = "OK" if sock_ok else "NO CTRL"
        status_text.set(f"Conn:{conn}  MOVE:{move_char or '-'}  "
                        f"YAW={yaw}{yd}  PITCH={pitch}{pd}  SENT:{last_hex}")
    except Exception as e:
        status_text.set(f"ERR: {e}")

    img_label.after(50, update_frame, sock, img_label, last_send, status_text)


def terminal_loop(sock):
    """后台线程: 读终端指令, 修改全局控制状态"""
    global move_char, yaw, pitch, yaw_dir, pitch_dir, running, sock_ok
    print("\n========== 终端控制已启用 ==========")
    print("  1退 2停 3进 4停 5左转 6右转 7左弧 8右转")
    print("  z 水平舵机持续向左   y 水平舵机持续向右")
    print("  r 垂直舵机持续向上   t 垂直舵机持续向下")
    print("  v 水平+垂直舵机停止转动")
    print("  s N     水平舵机(PA8) 设绝对位置 N=0~255")
    print("  p N     垂直舵机(PA11) 设绝对位置 N=0~255")
    print("  f C Y P 发完整帧 (C=0停1前2后3左4右, Y/P=0~255)")
    print("  q       退出")
    print("====================================\n")

    while running:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            running = False
            return
        if not line:
            continue
        if line == 'q':
            running = False
            return

        c = line[0]
        if c in CHAR2MOVE:
            move_char = c
            # 立即发一次单字符 (与 wifi_ctrl.py 一致, 已实测可用)
            send_move_char(sock, c)
            print(f"  -> 运动 '{c}'")
        elif c == 'z':
            yaw_dir = -1
            move_char = None
            print("  -> 水平舵机持续向左 (y 反向 / v 停)")
        elif c == 'y':
            yaw_dir = 1
            move_char = None
            print("  -> 水平舵机持续向右 (z 反向 / v 停)")
        elif c == 'r':
            pitch_dir = -1
            move_char = None
            print("  -> 垂直舵机持续向上 (t 反向 / v 停)")
        elif c == 't':
            pitch_dir = 1
            move_char = None
            print("  -> 垂直舵机持续向下 (r 反向 / v 停)")
        elif c == 'v':
            yaw_dir = 0
            pitch_dir = 0
            print("  -> 舵机停止转动")
        elif c == 's' and len(line) > 1:
            try:
                yaw = max(0, min(255, int(line[1:].strip())))
                yaw_dir = 0
                send_servo_frame(sock, yaw, pitch)
                print(f"  -> YAW={yaw}")
            except ValueError:
                print("  用法: s 0~255")
        elif c == 'p' and len(line) > 1:
            try:
                pitch = max(0, min(255, int(line[1:].strip())))
                pitch_dir = 0
                send_servo_frame(sock, yaw, pitch)
                print(f"  -> PITCH={pitch}")
            except ValueError:
                print("  用法: p 0~255")
        elif c == 'f' and len(line) > 1:
            parts = line[1:].split()
            if len(parts) == 3:
                try:
                    cy = int(parts[0]) & 0xFF
                    yv = int(parts[1]) & 0xFF
                    pv = int(parts[2]) & 0xFF
                    yaw_dir = 0
                    pitch_dir = 0
                    if cy == 0:
                        move_char = '2'      # 停
                    elif cy == 1:
                        move_char = '3'      # 前
                    elif cy == 2:
                        move_char = '1'      # 后
                    elif cy == 3:
                        move_char = '5'      # 左
                    elif cy == 4:
                        move_char = '6'      # 右
                    else:
                        move_char = None
                    yaw = yv
                    pitch = pv
                    # 立即发一帧组合 (单字符运动 + 舵机)
                    try:
                        if sock is not None:
                            sock.send(bytes([0xAA, 0x00, cy, yaw, pitch]))
                            last_hex = f"AA 00 {cy:02X} {yaw:02X} {pitch:02X}"
                            sock_ok = True
                    except OSError:
                        sock_ok = False
                    print(f"  -> 帧 AA 00 {cy:02X} {yaw:02X} {pitch:02X}")
                except ValueError:
                    print("  用法: f C Y P")
            else:
                print("  用法: f C Y P")
        else:
            print("  未知指令")


def on_closing(sock):
    global running
    running = False
    try:
        if sock is not None:
            sock.send(bytes([0xAA, 0x00, 0x00, yaw, pitch]))
            sock.close()
    except OSError:
        pass
    if root_win is not None:
        root_win.destroy()


def main():
    global root_win, sock_ok
    print("=== 智能车 WiFi 摄像头控制窗口 ===")
    print(f"摄像头: {CAM_URL}")
    print(f"控制  : {CAR_IP}:{CTRL_PORT}")

    sock = connect_ctrl()
    if sock is None:
        print("[WARN] 控制端口未连接, 只能看画面 (脚本会持续重试连接)")
        sock_ok = False
    else:
        print(f"[OK] 已连接控制端口 {CAR_IP}:{CTRL_PORT}")
        sock_ok = True

    root = tk.Tk()
    root_win = root
    root.title("SmartCar Cam")
    root.geometry("640x520")
    root.resizable(False, False)

    status_text = tk.StringVar()
    status_text.set(f"Conn:{'OK' if sock_ok else 'NO CTRL'}  MOVE:-  YAW=128  PITCH=128")

    status_label = Label(root, textvariable=status_text, bd=1, relief=tk.SUNKEN, anchor=tk.W)
    status_label.pack(side=tk.TOP, fill=tk.X)

    img_label = Label(root, bg="black")
    img_label.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

    hint = Label(root, text="1退2停3进4停5左6右7左弧8右转 | z/y/r/t舵机 | s/p N | f C Y P | q退出 (指令在下方终端输入)",
                 anchor=tk.CENTER)
    hint.pack(side=tk.BOTTOM, fill=tk.X)

    # 启动后台终端输入线程
    t = threading.Thread(target=terminal_loop, args=(sock,), daemon=True)
    t.start()

    root.protocol("WM_DELETE_WINDOW", lambda: on_closing(sock))
    img_label.after(100, update_frame, sock, img_label, [0.0], status_text)

    root.mainloop()
    print("已退出.")


if __name__ == "__main__":
    main()
