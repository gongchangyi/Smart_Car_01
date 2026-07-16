#!/usr/bin/env python3
# 简易 WiFi 图传模块控制脚本 (PC 端)
# 连接 192.168.1.1:2002 (ser2net -> STM32 USART3/PB10/PB11/9600)
# 运行: python wifi_ctrl.py   (电脑需先连上模块热点 ZZXYD-xxxx)
import socket
import time

HOST = "192.168.1.1"
PORT = 2002


def connect():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3)
    sock.connect((HOST, PORT))
    return sock


def send_packet(sock, packet):
    """模块关闭旧连接时，自动重连一次后重发。"""
    try:
        sock.sendall(packet)
        show_reply(sock)
        return sock, True
    except OSError as first_error:
        try:
            sock.close()
            sock = connect()
            sock.sendall(packet)
            show_reply(sock)
            print("连接已重建，指令已重发")
            return sock, True
        except OSError as second_error:
            print(f"发送失败: {first_error}; 重连失败: {second_error}")
            return sock, False


def show_reply(sock):
    """显示 STM32 的诊断回传；超时表示当前固件没有经 TCP 回传。"""
    try:
        time.sleep(0.05)
        sock.settimeout(0.2)
        reply = sock.recv(64)
        if reply:
            print("小车回传:", reply.decode("ascii", errors="replace").strip())
    except socket.timeout:
        print("小车回传: 无")
    except OSError as error:
        print(f"读取回传失败: {error}")
    finally:
        sock.settimeout(3)


def main():
    try:
        s = connect()
    except Exception as e:
        print(f"连接失败: {e}")
        print("确认: 1)电脑已连模块热点 ZZXYD-xxxx  2)模块 ser2net 在 2002 监听  3)IP/端口正确")
        return

    print(f"已连接 {HOST}:{PORT}")
    print("指令:")
    print("  1~8   单字符控车 (1退 2停 3进 4停 5左转圈 6右转圈 7左弧 8右弧)")
    print("  s N   只转水平舵机(PA8) N=0~255 -> 发 AA 00 00 N 80 (车不动)")
    print("  p N   只转垂直舵机(PA11) N=0~255 -> 发 AA 00 00 80 N (车不动)")
    print("  f C Y P 发完整帧 AA 00 C Y P (C=cmd:0停1前2后3左4右, Y=水平, P=垂直, 各0~255)")
    print("  q     退出")

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not line:
            continue
        if line == 'q':
            break
        if line[0] in '12345678':
            s, ok = send_packet(s, line[0].encode())
            if ok: print("发:", line[0])
        elif line[0] == 's' and len(line) > 1:
            try:
                n = int(line[1:].strip()) & 0xFF
                s, ok = send_packet(s, bytes([0xAA, 0x00, 0x00, n, 0x80]))
                if ok: print(f"发水平舵机帧 AA 00 00 {n:02X} 80  (水平≈{n/255*180:.0f}°)")
            except ValueError:
                print("用法: s 0~255")
        elif line[0] == 'p' and len(line) > 1:
            try:
                n = int(line[1:].strip()) & 0xFF
                s, ok = send_packet(s, bytes([0xAA, 0x00, 0x00, 0x80, n]))
                if ok: print(f"发垂直舵机帧 AA 00 00 80 {n:02X}  (垂直≈{n/255*180:.0f}°)")
            except ValueError:
                print("用法: p 0~255")
        elif line[0] == 'f' and len(line) > 1:
            parts = line[1:].split()
            if len(parts) == 3:
                c = int(parts[0]) & 0xFF
                y = int(parts[1]) & 0xFF
                p = int(parts[2]) & 0xFF
                s, ok = send_packet(s, bytes([0xAA, 0x00, c, y, p]))
                if ok: print(f"发帧 AA 00 {c:02X} {y:02X} {p:02X}")
            else:
                print("用法: f C Y P")
        else:
            print("未知指令")

    s.close()
    print("已断开")

if __name__ == "__main__":
    main()
