@echo off
start "" /wait "%~dp0bin\BatteryIndicator.exe" --install
if errorlevel 1 exit /b 1
start "" "%~dp0bin\BatteryIndicator.exe"
