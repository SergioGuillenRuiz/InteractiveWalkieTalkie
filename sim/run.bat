@echo off
REM ============================================================
REM  Compila y ejecuta el simulador.
REM    run.bat                     -> modo interactivo (teclado)
REM    run.bat scripts\nav.sim     -> ejecuta un script
REM    run.bat --color scripts\... -> con color en el render
REM ============================================================
setlocal
set "SIM=%~dp0"
call "%SIM%build.bat"
if errorlevel 1 exit /b 1
if "%~1"=="" (
  "%SIM%out\walkie_sim.exe" --interactive --color
) else (
  "%SIM%out\walkie_sim.exe" %*
)
