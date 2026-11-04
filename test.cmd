@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
cl /nologo /utf-8 /W4 /O1 /MT /DUNICODE /D_UNICODE tests\protocol_test.c /Fobin\protocol_test.obj /Febin\protocol_test.exe /link /SUBSYSTEM:CONSOLE setupapi.lib hid.lib user32.lib gdi32.lib shell32.lib advapi32.lib
if errorlevel 1 exit /b 1
bin\protocol_test.exe
