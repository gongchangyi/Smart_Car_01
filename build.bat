@echo off
setlocal

echo ============================================
echo   Smart_Car Build (ARM GCC + make)
echo ============================================
echo.

set "PATH=D:\STM32_Dev_Env\01.Compiler\arm-gcc\gcc-arm-none-eabi-10.3-2021.10\bin;C:\Program Files (x86)\GnuWin32\bin;%PATH%"

cd /d "%~dp0"

echo [1/2] Cleaning...
make clean

echo.
echo [2/2] Building...
make
if errorlevel 1 (
    echo.
    echo ***** BUILD FAILED *****
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo   BUILD OK!
echo   HEX: %~dp0build\Smart_Car.hex
echo ============================================
echo   Use ST-Link Utility to flash this hex.
echo.
pause
