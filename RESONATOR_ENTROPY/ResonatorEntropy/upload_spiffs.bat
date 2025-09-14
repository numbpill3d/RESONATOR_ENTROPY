@echo off
echo ==================================
echo ESP32 Resonator Entropy SPIFFS Uploader
echo ==================================
echo.
echo This script will help you upload the web interface files to your ESP32.
echo.

echo Instructions:
echo 1. Open Arduino IDE
echo 2. Go to Tools menu
echo 3. Select ESP32 Sketch Data Upload
echo 4. Wait for the upload to complete
echo.
echo If you don't see "ESP32 Sketch Data Upload" option, please install
echo the ESP32 Filesystem Uploader plugin from:
echo https://github.com/me-no-dev/arduino-esp32fs-plugin/releases
echo.
echo Press any key to open the project in Arduino IDE...
pause > nul

start "" "C:\Users\adoni\Documents\RESONATOR_ENTROPY\ResonatorEntropy\ResonatorEntropy.ino"

echo.
echo Project opened in Arduino IDE.
echo Don't forget to select the correct board and port before uploading.
echo.
pause
