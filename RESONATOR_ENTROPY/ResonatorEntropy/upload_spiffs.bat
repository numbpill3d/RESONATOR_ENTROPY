@echo off
title ESP32 SPIFFS Upload Tool - Fixed Version
echo ========================================
echo ESP32 Resonator Entropy SPIFFS Uploader
echo ========================================
echo Using port: COM4
echo mkspiffs tool: C:\Users\adoni\Documents\ESP32\mkspiffs\mkspiffs.exe
echo esptool: C:\Users\adoni\Documents\ESP32\esptool\esptool.exe
echo ========================================

REM Check if data folder exists, create if not
if not exist "data" (
    echo [INFO] data folder not found! Creating an empty one...
    mkdir data
    echo [INFO] Created empty data folder. The web UI uses embedded assets in the .ino - this is OK.
)

REM Check if tools exist
if not exist "C:\Users\adoni\Documents\ESP32\mkspiffs\mkspiffs.exe" (
    echo [ERROR] mkspiffs.exe not found at C:\Users\adoni\Documents\ESP32\mkspiffs\mkspiffs.exe
    echo Download from: https://github.com/igrr/mkspiffs/releases
    pause
    exit /b 1
)
if not exist "C:\Users\adoni\Documents\ESP32\esptool\esptool.exe" (
    echo [ERROR] esptool.exe not found at C:\Users\adoni\Documents\ESP32\esptool\esptool.exe
    echo Download from: https://github.com/espressif/esptool/releases
    pause
    exit /b 1
)

REM Set variables
set ESPPORT=COM4
set MKSPIFFS=C:\Users\adoni\Documents\ESP32\mkspiffs\mkspiffs.exe
set ESPTOOL=C:\Users\adoni\Documents\ESP32\esptool\esptool.exe
set DATA_DIR=data
set IMAGE_FILE=spiffs.bin
set SIZE=1048576

REM Build SPIFFS image (1MB in bytes - fixed parse error)
echo [STEP 1] Building SPIFFS image from %DATA_DIR% (size: %SIZE% bytes)...
%MKSPIFFS% -c %DATA_DIR% -p 256 -b 4096 -s %SIZE% %IMAGE_FILE%

if %ERRORLEVEL% neq 0 (
    echo [ERROR] mkspiffs failed. Check data folder contents and partition scheme in Arduino IDE.
    echo Tip: Ensure Tools ^> Partition Scheme is "Default 4MB with spiffs".
    if exist %IMAGE_FILE% del %IMAGE_FILE%
    pause
    exit /b 1
)

echo [SUCCESS] SPIFFS image built: %IMAGE_FILE% (%SIZE% bytes allocated)

REM Upload to ESP32
echo [STEP 2] Uploading %IMAGE_FILE% to ESP32 on %ESPPORT% at address 0x290000...
%ESPTOOL% --chip esp32 --port %ESPPORT% --baud 115200 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB -z 0x290000 %IMAGE_FILE%

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Upload failed. Try holding BOOT button on ESP32 during connection.
    echo Tip: Check port in Device Manager, or change ESPPORT=COM4 to your port.
    if exist %IMAGE_FILE% del %IMAGE_FILE%
    pause
    exit /b 1
)

REM Cleanup
if exist %IMAGE_FILE% del %IMAGE_FILE%

echo ========================================
echo [SUCCESS] SPIFFS upload complete!
echo Next: Press RESET on ESP32, connect to "EntropyReader" WiFi (pwd: entropy123)
echo Visit http://192.168.4.1 for the black/red web UI.
echo ========================================
pause