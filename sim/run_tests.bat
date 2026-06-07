@echo off
REM ============================================================
REM  Compila el simulador y ejecuta todos los scripts de prueba.
REM  Codigo de salida != 0 si alguna suite falla (util para CI).
REM ============================================================
setlocal enabledelayedexpansion
set "SIM=%~dp0"
set "EXE=%SIM%out\walkie_sim.exe"

call "%SIM%build.bat"
if errorlevel 1 exit /b 1

set /a TOTAL=0
set /a OK=0
echo.
echo ============================================
echo  Tests del simulador
echo ============================================
for %%f in ("%SIM%scripts\*.sim") do (
  if /I not "%%~nxf"=="smoke.sim" (
    "%EXE%" --fresh "%%f" > "%TEMP%\_simres.txt" 2>&1
    set "RC=!errorlevel!"
    set "LINE="
    for /f "tokens=*" %%r in ('findstr /C:"Resultado" "%TEMP%\_simres.txt"') do set "LINE=%%r"
    set /a TOTAL+=1
    if "!RC!"=="0" (
      set /a OK+=1
      echo   [ OK ] %%~nxf  -  !LINE!
    ) else (
      echo   [FAIL] %%~nxf  -  !LINE!
      type "%TEMP%\_simres.txt" | findstr /C:"[FAIL]"
    )
  )
)
echo ============================================
echo  Suites correctas: !OK!/!TOTAL!
echo ============================================
del "%TEMP%\_simres.txt" >nul 2>&1
if not "!OK!"=="!TOTAL!" exit /b 1
exit /b 0
