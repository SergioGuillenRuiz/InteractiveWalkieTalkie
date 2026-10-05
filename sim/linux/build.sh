#!/usr/bin/env bash
# ============================================================
#  Compila el simulador nativo del walkie-talkie en Linux (g++/clang++).
#  Compila el firmware REAL de ../../src + la capa mock Arduino.
#  Salida: sim/out/linux/walkie_sim  (separada de la build de Windows)
#
#  Compilacion incremental: cada fuente se recompila solo si cambio ella o
#  alguna cabecera del proyecto. Variables: CXX, CC, JOBS.
# ============================================================
set -euo pipefail

LNX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(dirname "$LNX")"
ROOT="$(dirname "$SIM")"
OUT="$SIM/out/linux"
OBJ="$OUT/obj"
EXE="$OUT/walkie_sim"

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "[build] No se encontro el compilador C++ ($CXX). Instala build-essential / gcc-c++."
  exit 1
fi
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "[build] No se encontro el compilador C ($CC)."
  exit 1
fi

mkdir -p "$OBJ"

INC=(-I"$LNX" -I"$SIM/arduino" -I"$SIM/vendor/gfx" -I"$SIM/vendor/aes" -I"$SIM/engine" -I"$ROOT/include")
DEFS=(-DARDUINO=100 -DCBC=1 -DAES128=1)
CXXFLAGS=(-std=c++17 -O2 -w "${DEFS[@]}" "${INC[@]}")
CFLAGS=(-O2 -w "${DEFS[@]}" "${INC[@]}")

SRCS=(
  "$ROOT"/src/*.cpp
  "$SIM/engine/sim_runtime.cpp"
  "$SIM/engine/lora_mock.cpp"
  "$SIM/engine/air_channel.cpp"
  "$SIM/engine/framebuffer.cpp"
  "$SIM/engine/sim_main.cpp"
  "$LNX/console_linux.cpp"
  "$SIM/vendor/gfx/Adafruit_GFX.cpp"
  "$SIM/vendor/aes/aes.c"
)

# Cabecera mas reciente: si alguna cambia, se recompila todo.
newest_hdr=$(find "$ROOT/include" "$SIM/arduino" "$SIM/engine" "$SIM/vendor" "$LNX" \
               -name '*.h' -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -d' ' -f2-)

echo "[build] Compilando..."
OBJS=()
pids=()
fail=0
for src in "${SRCS[@]}"; do
  rel="${src#"$ROOT"/}"
  obj="$OBJ/${rel//\//_}.o"
  OBJS+=("$obj")
  # -nt compara con precision de nanosegundos (un cambio en el mismo segundo cuenta).
  if [[ -f "$obj" && ! "$src" -nt "$obj" && ( -z "$newest_hdr" || ! "$newest_hdr" -nt "$obj" ) ]]; then
    continue
  fi
  if [[ "$src" == *.c ]]; then
    "$CC" "${CFLAGS[@]}" -c "$src" -o "$obj" &
  else
    "$CXX" "${CXXFLAGS[@]}" -c "$src" -o "$obj" &
  fi
  pids+=($!)
  if (( ${#pids[@]} >= JOBS )); then
    wait "${pids[0]}" || fail=1
    pids=("${pids[@]:1}")
  fi
done
for p in "${pids[@]}"; do wait "$p" || fail=1; done

if (( fail )); then
  echo "[build] BUILD FAILED"
  exit 1
fi

"$CXX" "${OBJS[@]}" -o "$EXE" -lpthread || { echo "[build] BUILD FAILED (enlazado)"; exit 1; }
echo "[build] OK -> $EXE"
