#!/usr/bin/env bash
# ============================================================
#  Compila el simulador y ejecuta todos los scripts de prueba.
#  Codigo de salida != 0 si alguna suite falla (util para CI).
#  Equivalente Linux de sim/run_tests.bat.
# ============================================================
set -uo pipefail

LNX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(dirname "$LNX")"
EXE="$SIM/out/linux/walkie_sim"

"$LNX/build.sh" || exit 1

RES="$(mktemp)"
trap 'rm -f "$RES"' EXIT

total=0
ok=0
echo
echo "============================================"
echo " Tests del simulador"
echo "============================================"
for f in "$SIM"/scripts/*.sim; do
  name="$(basename "$f")"
  [[ "$name" == "smoke.sim" ]] && continue
  "$EXE" --fresh "$f" </dev/null >"$RES" 2>&1
  rc=$?
  line="$(grep -F "Resultado" "$RES" | tail -1)"
  total=$((total + 1))
  if [[ $rc -eq 0 ]]; then
    ok=$((ok + 1))
    echo "  [ OK ] $name  -  $line"
  else
    echo "  [FAIL] $name  -  $line"
    grep -F "[FAIL]" "$RES"
  fi
done
echo "============================================"
echo " Suites correctas: $ok/$total"
echo "============================================"
[[ $ok -eq $total ]]
