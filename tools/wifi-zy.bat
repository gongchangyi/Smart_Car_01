@echo off
REM 双击运行: 打开智能车 WiFi 摄像头控制窗口 (看画面 + 键盘控车)
REM 前提: 电脑已连接模块热点 ZZXYD-xxxx

cd /d "%~dp0"
python wifi_cam.py
pause
