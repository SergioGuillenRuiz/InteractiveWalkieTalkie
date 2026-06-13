# ============================================================
#  Lanzador MULTI-DISPOSITIVO interactivo.
#
#  Abre N ventanas, cada una un dispositivo REAL e independiente (firmware +
#  EEPROM propios) que se comunican por radio simulada en tiempo real (un "aire"
#  compartido). Lo que envia uno lo reciben los demas, igual que el hardware real.
#
#  Controles en cada ventana: [m] Morse  [n] Finish  [Shift+M/N] pulsacion larga
#                             [flechas] potenciometro  [q] salir
#
#  Uso:  net.bat [N]      (por defecto 2 dispositivos)
# ============================================================
param([int]$N = 2)
$ErrorActionPreference = "Stop"
$sim  = "$PSScriptRoot\out\walkie_sim.exe"
$air  = "$PSScriptRoot\out\air"
$work = "$PSScriptRoot\out"

# Compilar
& "$env:ComSpec" /c "$PSScriptRoot\build.bat" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "La compilacion fallo" }

# Aire nuevo (sesion limpia)
if (Test-Path $air) { Remove-Item "$air\*" -Force -ErrorAction SilentlyContinue }
else { New-Item -ItemType Directory -Path $air | Out-Null }

Write-Host "Abriendo $N dispositivos que se comunican por el aire compartido."
Write-Host "Cierra las ventanas (o pulsa [q] en cada una) para terminar.`n"

for ($i = 1; $i -le $N; $i++) {
    $env:SIM_CHIPID = "$($i * 100)"   # id de equipo distinto por dispositivo
    Start-Process -FilePath "cmd.exe" -ArgumentList @(
        "/c", "title Walkie #$i && `"$sim`" --interactive --color " +
              "--air `"$air`" --node $i --eeprom `"$work\net_dev$i.bin`" --fresh"
    )
    Start-Sleep -Milliseconds 200
}
Remove-Item Env:\SIM_CHIPID -ErrorAction SilentlyContinue
Write-Host "Listo. $N dispositivos en marcha."
