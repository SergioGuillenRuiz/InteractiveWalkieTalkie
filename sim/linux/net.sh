#!/usr/bin/env bash
# ============================================================
#  Lanzador MULTI-DISPOSITIVO interactivo (equivalente Linux de net.bat/net.ps1).
#
#  Abre N ventanas de GNOME Terminal, cada una un dispositivo REAL e
#  independiente (firmware + EEPROM propios) que se comunican por radio simulada
#  en tiempo real (un "aire" compartido). Lo que envia uno lo reciben los demas.
#
#  Controles en cada ventana: [m] Morse  [n] Finish  [Shift+M/N] pulsacion larga
#                             [flechas] potenciometro  [q] salir
#
#  Uso:  ./net.sh [N]      (por defecto 2 dispositivos)
# ============================================================
set -euo pipefail

N="${1:-2}"
LNX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(dirname "$LNX")"
WORK="$SIM/out/linux"
EXE="$WORK/walkie_sim"
AIR="$WORK/air"
source "$LNX/term_common.sh"

"$LNX/build.sh" >/dev/null || { echo "La compilacion fallo"; exit 1; }

if ! sim_have_gnome_terminal; then
  echo "net.sh necesita una sesion grafica con gnome-terminal."
  echo "Alternativa: abre $N terminales y en cada uno ejecuta (i = 1..$N):"
  echo "  SIM_CHIPID=\$((i*100)) $EXE --interactive --color --air $AIR --node \$i --eeprom $WORK/net_dev\$i.bin --fresh"
  exit 1
fi

# Aire nuevo (sesion limpia)
mkdir -p "$AIR"
rm -f "$AIR"/* 2>/dev/null || true

echo "Abriendo $N dispositivos que se comunican por el aire compartido."
echo "Cierra las ventanas (o pulsa [q] en cada una) para terminar."
echo

zoom="$(sim_zoom)"
for ((i = 1; i <= N; i++)); do
  # id de equipo distinto por dispositivo
  gnome-terminal --title="Walkie #$i" \
    --geometry="${SIM_TERM_COLS}x${SIM_TERM_ROWS}" --zoom="$zoom" \
    -- env SIM_CHIPID="$((i * 100))" bash -c '
      source "$1/term_common.sh"
      sim_run_restoring_tty "$2" --interactive --color --air "$3" --node "$4" --eeprom "$5" --fresh
    ' _ "$LNX" "$EXE" "$AIR" "$i" "$WORK/net_dev$i.bin"
  sleep 0.2
done
echo "Listo. $N dispositivos en marcha."
