@echo off
REM ============================================================
REM  Lanza N dispositivos del simulador comunicandose por radio
REM  (aire compartido), cada uno en su ventana. Uso: net.bat [N]
REM ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0net.ps1" %*
