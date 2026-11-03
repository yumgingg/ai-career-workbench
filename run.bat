@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist zysygh.exe (
    echo 未找到 zysygh.exe，先运行 build.bat 编译。
    pause
    exit /b 1
)
zysygh.exe
