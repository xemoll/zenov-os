@echo off
setlocal
set "ISO_IMAGE=%~dp0ZenovOS-0.1.1-x86.iso"
set "BOOT_IMAGE=%~dp0ZenovOS-0.1.1-x86.img"
set "DATA_IMAGE=%~dp0ZenovOS-0.1.1-data.img"

where qemu-system-i386.exe >nul 2>nul
if errorlevel 1 (
  echo qemu-system-i386.exe was not found. Install QEMU and add it to PATH.
  exit /b 1
)

if not exist "%DATA_IMAGE%" (
  echo Missing persistent data image: %DATA_IMAGE%
  exit /b 1
)

if exist "%ISO_IMAGE%" (
  qemu-system-i386.exe -m 64M -drive file="%DATA_IMAGE%",format=raw,if=ide,index=0,media=disk -drive file="%ISO_IMAGE%",format=raw,if=ide,index=2,media=cdrom,readonly=on -boot order=d,strict=on
  exit /b %errorlevel%
)

if not exist "%BOOT_IMAGE%" (
  echo Missing boot media: expected %ISO_IMAGE% or %BOOT_IMAGE%
  exit /b 1
)

qemu-system-i386.exe -m 64M -drive file="%BOOT_IMAGE%",format=raw,if=floppy -drive file="%DATA_IMAGE%",format=raw,if=ide,index=0,media=disk -boot a
