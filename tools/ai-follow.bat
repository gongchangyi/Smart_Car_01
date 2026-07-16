@echo off
REM 人体识别跟随。电脑需连接小车热点，首次运行请安装 ultralytics 和 opencv-python。
cd /d "%~dp0"
python ai_follow.py
pause
