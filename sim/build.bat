@echo off
REM ============================================================
REM  Compila el simulador nativo del walkie-talkie con MSVC.
REM  Compila el firmware REAL de ../src + la capa mock Arduino.
REM ============================================================
setlocal
set "SIM=%~dp0"
set "ROOT=%SIM%.."

REM --- Localizar Visual Studio Build Tools (vswhere) ---
set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSINSTALLER%\vswhere.exe" goto :no_vswhere

set "VSPATH="
"%VSINSTALLER%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\_vspath.txt" 2>nul
set /p VSPATH=<"%TEMP%\_vspath.txt"
del "%TEMP%\_vspath.txt" >nul 2>&1
if not defined VSPATH goto :no_msvc

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 goto :no_env

if not exist "%SIM%out" mkdir "%SIM%out"
pushd "%SIM%out"

set INC=/I"%SIM%arduino" /I"%SIM%vendor\gfx" /I"%SIM%vendor\aes" /I"%SIM%engine" /I"%ROOT%\include"

echo [build] Compilando...
cl /nologo /EHsc /std:c++17 /O2 /DARDUINO=100 /D_CRT_SECURE_NO_WARNINGS /DCBC=1 /DAES128=1 /wd4244 /wd4267 /wd4146 /wd4005 %INC% "%ROOT%\src\*.cpp" "%SIM%engine\sim_runtime.cpp" "%SIM%engine\lora_mock.cpp" "%SIM%engine\air_channel.cpp" "%SIM%engine\framebuffer.cpp" "%SIM%engine\console_win.cpp" "%SIM%engine\sim_main.cpp" "%SIM%vendor\gfx\Adafruit_GFX.cpp" "%SIM%vendor\aes\aes.c" /Fe"walkie_sim.exe"
set "RC=%errorlevel%"
popd
if not "%RC%"=="0" goto :failed

echo [build] OK -^> %SIM%out\walkie_sim.exe
exit /b 0

:no_vswhere
echo [build] No se encontro vswhere.exe. Instala Visual Studio Build Tools.
exit /b 1
:no_msvc
echo [build] No se encontro el compilador MSVC (VC Tools).
exit /b 1
:no_env
echo [build] Fallo al inicializar el entorno de MSVC.
exit /b 1
:failed
echo [build] BUILD FAILED
exit /b 1
