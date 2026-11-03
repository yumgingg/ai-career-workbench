@echo off
chcp 65001 >nul
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [错误] 未找到 g++，请先安装 MinGW-w64 并把 bin 目录加入 PATH。
    pause
    exit /b 1
)

echo 正在编译 src\zysygh.cpp ...
g++ -std=c++17 -O2 -static -Wall -Wextra -o zysygh.exe src\zysygh.cpp
if errorlevel 1 (
    echo [错误] 编译失败，请检查上面的报错信息。
    pause
    exit /b 1
)

echo.
echo 编译成功：%~dp0zysygh.exe
echo 直接运行 run.bat 或双击 zysygh.exe 即可。
pause
