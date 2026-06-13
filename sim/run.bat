@echo off
REM ============================================================
REM  Compila y ejecuta el simulador.
REM    run.bat                     -> modo interactivo (teclado)
REM    run.bat scripts\nav.sim     -> ejecuta un script
REM    run.bat --color scripts\... -> con color en el render
REM    run.bat --keys guion.txt    -> reproduce teclas con guion (demo/prueba)
REM ============================================================
setlocal
set "SIM=%~dp0"
call "%SIM%build.bat"
if errorlevel 1 exit /b 1
if "%~1"=="" (
  REM Forzar consola clasica (conhost): Windows Terminal ignora la API de fuente,
  REM asi que la usamos para fijar fuente/tamano y ver pixeles CUADRADOS de serie,
  REM sin tener que reducir el zoom a mano.
  conhost.exe "%SIM%out\walkie_sim.exe" --interactive --color
) else (
  "%SIM%out\walkie_sim.exe" %*
)
