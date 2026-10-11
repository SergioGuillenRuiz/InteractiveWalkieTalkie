#!/usr/bin/env bash
# ============================================================
#  Ejecuta TODAS las suites de scripts/ con varias secuencias aleatorias (SIM_SEED).
#
#  Cada equipo simulado sortea retardos de ACK, dispersion de reintentos y balizas, pausas de escucha-antes-de-
#  hablar...: un test que solo pasa con la secuencia por defecto (porque depende de una combinacion afortunada de
#  tiempos) falla en alguna semilla. La identidad del equipo no cambia con la semilla.
#
#  Uso:  ./seed_tests.sh [desde=1] [hasta=20] [paralelo=4]
#        SIM_SEED=<n> ../out/linux/walkie_sim --fresh ../scripts/<suite>.sim   # reproducir un fallo
#  Codigo de salida != 0 si alguna combinacion suite/semilla falla.
# ============================================================
set -uo pipefail

LNX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(dirname "$LNX")"
EXE="$SIM/out/linux/walkie_sim"
FROM="${1:-1}"; TO="${2:-20}"; PAR="${3:-4}"

"$LNX/build.sh" >/dev/null || { echo "build fallo"; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK:?}"' EXIT

# run_one <suite> <semilla>: imprime "FALLA suite semilla" si la suite no termina con 0 FAIL.
run_one() {
  local suite="$1" seed="$2" log; log="$WORK/${suite}_${seed}.log"
  SIM_SEED="$seed" "$EXE" --fresh --eeprom "$WORK/${suite}_${seed}.bin" "$SIM/scripts/$suite.sim" </dev/null >"$log" 2>&1
  local rc=$?
  if (( rc != 0 )); then
    echo "FALLA $suite semilla $seed: $(grep -F '[FAIL]' "$log" | head -2 | tr -s ' ' | tr '\n' ';')"
  fi
}
export -f run_one
export EXE SIM WORK

suites=()
for f in "$SIM"/scripts/*.sim; do
  n="$(basename "$f" .sim)"
  [[ "$n" == "smoke" ]] && continue
  suites+=("$n")
done

echo "== ${#suites[@]} suites x semillas $FROM..$TO ($PAR en paralelo) =="
for s in "${suites[@]}"; do for seed in $(seq "$FROM" "$TO"); do echo "$s $seed"; done; done \
  | xargs -P "$PAR" -L 1 bash -c 'run_one "$0" "$1"' > "$WORK/fallos.txt"

total=$(( ${#suites[@]} * (TO - FROM + 1) ))
bad=$(wc -l < "$WORK/fallos.txt")
sort -k2,2 -k4,4n "$WORK/fallos.txt"
echo "== $((total - bad))/$total ejecuciones correctas =="
(( bad == 0 ))
