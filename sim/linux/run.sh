#!/usr/bin/env bash
# ============================================================
#  Compila y ejecuta el simulador (equivalente Linux de sim/run.bat).
#    ./run.sh                       -> modo interactivo en una ventana nueva de
#                                      GNOME Terminal ya dimensionada
#    ./run.sh --here                -> modo interactivo en ESTE terminal
#    ./run.sh ../scripts/nav.sim    -> ejecuta un script
#    ./run.sh --color ../scripts/.. -> con color en el render
#    ./run.sh --keys guion.txt      -> reproduce teclas con guion (demo/prueba)
# ============================================================
set -euo pipefail

LNX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(dirname "$LNX")"
EXE="$SIM/out/linux/walkie_sim"
source "$LNX/term_common.sh"

if [[ "${1:-}" == "--here" ]]; then
  # Ya compilado por la invocacion que abrio esta ventana (o compilar ahora).
  [[ -x "$EXE" && "${SIM_SKIP_BUILD:-}" == "1" ]] || "$LNX/build.sh"
  sim_run_restoring_tty "$EXE" --interactive --color
  exit $?
fi

"$LNX/build.sh"

if [[ $# -eq 0 ]]; then
  if sim_have_gnome_terminal; then
    # Ventana nueva con el tamano y zoom justos para ver pixeles CUADRADOS sin
    # tocar el zoom a mano (lo mismo que run.bat consigue lanzando conhost).
    gnome-terminal --title="Walkie-Talkie (simulador)" \
      --geometry="${SIM_TERM_COLS}x${SIM_TERM_ROWS}" --zoom="$(sim_zoom)" \
      -- env SIM_SKIP_BUILD=1 "$LNX/run.sh" --here
    echo "[run] Simulador abierto en una ventana nueva de GNOME Terminal."
  else
    sim_run_restoring_tty "$EXE" --interactive --color
  fi
else
  exec "$EXE" "$@"
fi
