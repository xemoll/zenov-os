@echo off
setlocal
set "IMAGE=%~dp0ZenovOS-0.1.0-x86.img"

where qemu-system-i386.exe >nul 2>nul
if errorlevel 1 (
  echo qemu-system-i386.exe was not found. Install QEMU and add it to PATH.
  exit /b 1
)

qemu-system-i386.exe -drive file="%IMAGE%",format=raw,if=floppy -boot a -m 32M
