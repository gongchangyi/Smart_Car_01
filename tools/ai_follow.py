#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""YOLO 人体识别跟随控制工具。

电脑通过 WiFi 读取小车 MJPEG 视频流，用 YOLO 检测画面中的人，
并向 192.168.1.1:2002 发送信盈达六字节控制帧。按 q 或关闭窗口停车退出。
"""

import argparse
import socket
import sys
import time
from pathlib import Path

try:
    import cv2
    from ultralytics import YOLO
except ImportError as exc:
    print("缺少 AI 运行依赖: pip install ultralytics opencv-python")
    raise SystemExit(1) from exc


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MODEL = ROOT / "05.Documents" / "9.移动智能车（AI识别跟随）信盈达" / "1.项目资料" / "car_follow" / "yolov8n.pt"

CAR_IP = "192.168.1.1"
CTRL_PORT = 2002
STREAM_URL = f"http://{CAR_IP}:8080/?action=stream"

# 目标框面积占画面比例。目标偏小则前进，偏大则后退。
FOLLOW_AREA_MIN = 0.12
FOLLOW_AREA_MAX = 0.25
CENTER_DEADBAND = 0.16
CONFIDENCE = 0.50
COMMAND_PERIOD = 0.12
LOST_STOP_SECONDS = 0.70

CAR_CMD = {"stop": 0x00, "forward": 0x01, "backward": 0x02,
           "left": 0x03, "right": 0x04}


def frame(car_command, speed=0x0A):
    """构造 AA 55 | 小车 | 云台静止 | 速度 | 校验 的厂家控制帧。"""
    data = bytes((0xAA, 0x55, CAR_CMD[car_command], 0x05, speed))
    return data + bytes((sum(data) & 0xFF,))


class CarConnection:
    def __init__(self, host, port, dry_run=False):
        self.host = host
        self.port = port
        self.dry_run = dry_run
        self.socket = None

    def connect(self):
        if self.dry_run:
            return True
        self.close()
        try:
            self.socket = socket.create_connection((self.host, self.port), timeout=2)
            self.socket.settimeout(1)
            return True
        except OSError as exc:
            print(f"控制连接失败: {exc}")
            self.socket = None
            return False

    def send(self, command):
        packet = frame(command)
        if self.dry_run:
            print(command, packet.hex(" "))
            return True
        if self.socket is None and not self.connect():
            return False
        try:
            self.socket.sendall(packet)
            return True
        except OSError:
            self.close()
            return False

    def close(self):
        if self.socket is not None:
            try:
                self.socket.close()
            except OSError:
                pass
        self.socket = None


def choose_command(box, width, height):
    """优先把人转回画面中间，再按目标大小调整跟随距离。"""
    x1, y1, x2, y2, confidence = box
    center_x = (x1 + x2) / 2
    area_ratio = ((x2 - x1) * (y2 - y1)) / float(width * height)
    offset = (center_x - width / 2) / width

    if offset > CENTER_DEADBAND:
        command = "right"
    elif offset < -CENTER_DEADBAND:
        command = "left"
    elif area_ratio < FOLLOW_AREA_MIN:
        command = "forward"
    elif area_ratio > FOLLOW_AREA_MAX:
        command = "backward"
    else:
        command = "stop"
    return command, area_ratio, offset, confidence


def largest_person(result):
    best = None
    for box in result.boxes:
        confidence = float(box.conf[0])
        if confidence < CONFIDENCE:
            continue
        x1, y1, x2, y2 = map(float, box.xyxy[0].tolist())
        area = (x2 - x1) * (y2 - y1)
        if best is None or area > best[0]:
            best = (area, (x1, y1, x2, y2, confidence))
    return best[1] if best else None


def draw_overlay(image, box, command, area_ratio, offset):
    height, width = image.shape[:2]
    cv2.line(image, (width // 2, 0), (width // 2, height), (255, 255, 0), 1)
    label = f"{command}  area={area_ratio:.2f} offset={offset:.2f}"
    if box:
        x1, y1, x2, y2, confidence = box
        cv2.rectangle(image, (int(x1), int(y1)), (int(x2), int(y2)), (255, 0, 255), 2)
        label += f" conf={confidence:.2f}"
    cv2.putText(image, label, (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.65,
                (0, 255, 0), 2, cv2.LINE_AA)


def parse_args():
    parser = argparse.ArgumentParser(description="信盈达智能车人体识别跟随")
    parser.add_argument("--ip", default=CAR_IP, help="小车 WiFi 模块地址")
    parser.add_argument("--port", type=int, default=CTRL_PORT, help="控制端口")
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL, help="YOLO 模型路径")
    parser.add_argument("--dry-run", action="store_true", help="只显示将发送的控制帧")
    return parser.parse_args()


def main():
    args = parse_args()
    if not args.model.is_file():
        print(f"未找到模型文件: {args.model}")
        return 1

    stream_url = f"http://{args.ip}:8080/?action=stream"
    car = CarConnection(args.ip, args.port, args.dry_run)
    if not car.connect():
        return 1
    model = YOLO(str(args.model))
    cap = cv2.VideoCapture(stream_url)
    if not cap.isOpened():
        print(f"无法打开视频流: {stream_url}")
        car.send("stop")
        return 1

    last_sent = 0.0
    last_seen = time.monotonic()
    last_command = "stop"
    print("AI 跟随已启动，按 q 或 Esc 停车退出。")
    try:
        while True:
            ok, image = cap.read()
            now = time.monotonic()
            if not ok:
                if now - last_seen > LOST_STOP_SECONDS:
                    car.send("stop")
                time.sleep(0.05)
                continue

            result = model(image, classes=[0], verbose=False)[0]
            box = largest_person(result)
            if box:
                last_seen = now
                command, area_ratio, offset, _ = choose_command(box, image.shape[1], image.shape[0])
            else:
                command, area_ratio, offset = "stop", 0.0, 0.0

            if not box and now - last_seen > LOST_STOP_SECONDS:
                command = "stop"
            if command != last_command or now - last_sent >= COMMAND_PERIOD:
                car.send(command)
                last_command = command
                last_sent = now

            draw_overlay(image, box, command, area_ratio, offset)
            cv2.imshow("Smart Car AI Follow", image)
            key = cv2.waitKey(1) & 0xFF
            if key in (27, ord("q")):
                break
    finally:
        car.send("stop")
        car.close()
        cap.release()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
