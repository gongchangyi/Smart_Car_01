@echo off
setlocal
rem 把 ARM GCC 与 make 加入 PATH（与 build.bat 一致，已验证可用）
set "PATH=D:\STM32_Dev_Env\01.Compiler\arm-gcc\gcc-arm-none-eabi-10.3-2021.10\bin;C:\Program Files (x86)\GnuWin32\bin;%PATH%"
cd /d "%~dp0"
make clean
make
