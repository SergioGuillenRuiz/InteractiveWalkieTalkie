#!/usr/bin/env bash
# ============================================================
#  Prueba automatizada MULTI-DISPOSITIVO sobre el "aire" compartido
#  (equivalente Linux de sim/net_test.ps1).
#
#  Lanza dispositivos REALES concurrentes (procesos independientes, firmware +
#  EEPROM propios) que se comunican por radio simulada en tiempo real.
#
#  Escena A (3 equipos, difusion):  #1 emite -> #2 y #3 reciben y confirman (ACK);
#                                   #1 recibe la confirmacion; #1 NO se oye a si
#                                   mismo (half-duplex).
#  Escena B (2 equipos, bidireccional): #1 y #2 se envian mensajes a la vez; cada
#                                   uno recibe el del otro, lo confirma y NO oye
#                                   el propio. Prueba ambos sentidos del enlace.
#
#  Escena C (2 equipos, hora): #1 pone la hora a mano (y la corrige hacia atras)
#                              y #2 la adopta por radio.
#
#  Plantilla para testear futuras funciones de comunicacion multi-dispositivo.
#  Uso:  ./net_test.sh
# ============================================================
set -uo pipefail

LNX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(dirname "$LNX")"
WORK="$SIM/out/linux"
EXE="$WORK/walkie_sim"
# Tope de velocidad del tiempo virtual (ms virtuales por ms real). Sin tope cada proceso
# corre su tiempo casi instantaneo y los equipos dejan de coincidir en el aire (el que
# arranca unas decenas de ms despues se pierde lo ya emitido). 20 -> 14 s virtuales = 0,7 s.
PACE="${PACE:-20}"

# --- Compilar ---
"$LNX/build.sh" >/dev/null || { echo "build fallo"; exit 1; }

clear_air() { mkdir -p "$1"; rm -f "$1"/* 2>/dev/null || true; }

# start_node <air> <node> <chip> <keysText> <keysFile> <log>  -> deja el PID en $NODE_PID
start_node() {
  printf '%s\n' "$4" > "$WORK/$5"
  SIM_CHIPID="$3" "$EXE" --keys "$WORK/$5" --air "$1" --node "$2" --pace "$PACE" \
      --eeprom "$WORK/net_$2.bin" --fresh --shots "$WORK" </dev/null >"$6" 2>&1 &
  NODE_PID=$!
}

# Espera a los procesos dados con un limite de segundos (los mata si se pasa).
wait_all() {
  local timeout=$1; shift
  local deadline=$((SECONDS + timeout))
  for p in "$@"; do
    while kill -0 "$p" 2>/dev/null; do
      if (( SECONDS >= deadline )); then kill "$p" 2>/dev/null; break; fi
      sleep 0.2
    done
    wait "$p" 2>/dev/null
  done
}

fail=0
check() {
  if [[ "$2" == "1" ]]; then echo "  [PASS] $1"; else echo "  [FAIL] $1"; fail=$((fail + 1)); fi
}
has()    { grep -qE "$2" "$1" && echo 1 || echo 0; }
hasnot() { grep -qE "$2" "$1" && echo 0 || echo 1; }

# Guion: navegar IDLE -> Enviar -> Instant -> mensaje <potIdx> -> enviar; seguir vivo.
# pot 80   -> "Enviar"  (idx0 de 3 en IDLE).
# pot 384  -> modo "Instant" (idx1 de 4: Morse/Instant/Rueda/Frase).
# potIdx   -> mensaje del grid Instant (8 opciones): 585->"happy", 460->"kissy".
sender_keys() {
  cat <<EOF
0 pot 80
3000 mdown
3120 mup
3600 pot 384
4000 mdown
4120 mup
4600 pot $1
5000 mdown
5120 mup
$2 q
EOF
}

# Receptor: en reposo recibe; abre el historial y captura.
rx_keys() {
  cat <<EOF
0 pot 512
7000 mdown
7120 mup
9000 shot $1
10500 q
EOF
}

# ============================================================
#  ESCENA A: difusion 1 -> (2,3) + ACK + half-duplex
# ============================================================
echo
echo "==== Escena A: difusion 3 equipos (#1 -> #2,#3) ===="
AIR_A="$WORK/air_a"
clear_air "$AIR_A"

start_node "$AIR_A" 1 100 "$(sender_keys 585 10500)" "_a1.txt" "$WORK/_a1.out"; a1=$NODE_PID   # #1 envia "happy"
start_node "$AIR_A" 2 200 "$(rx_keys net_rx2)"        "_a2.txt" "$WORK/_a2.out"; a2=$NODE_PID
start_node "$AIR_A" 3 300 "$(rx_keys net_rx3)"        "_a3.txt" "$WORK/_a3.out"; a3=$NODE_PID
wait_all 60 "$a1" "$a2" "$a3"

o1="$WORK/_a1.out"; o2="$WORK/_a2.out"; o3="$WORK/_a3.out"
check "#1 emite 'happy'"                     "$(has "$o1" 'Chat\] Enviando: happy')"
check "#2 (id 201) recibe 'happy'"           "$(has "$o2" 'Guardado: happy')"
check "#3 (id 47) recibe 'happy'"            "$(has "$o3" 'Guardado: happy')"
check "#2 confirma con ACK a #101"           "$(has "$o2" 'ACK a #101')"
check "#3 confirma con ACK a #101"           "$(has "$o3" 'ACK a #101')"
check "#1 recibe la confirmacion (ACK)"      "$(has "$o1" 'Confirmado: entregado')"
check "half-duplex: #1 NO se oye a si mismo" "$(hasnot "$o1" 'ACK a #101')"

# ============================================================
#  ESCENA B: bidireccional 1 <-> 2 (cada uno emisor y receptor)
# ============================================================
echo
echo "==== Escena B: bidireccional 2 equipos (#1 <-> #2) ===="
AIR_B="$WORK/air_b"
clear_air "$AIR_B"

start_node "$AIR_B" 1 100 "$(sender_keys 585 14000)" "_b1.txt" "$WORK/_b1.out"; b1=$NODE_PID   # #1 envia "happy"
start_node "$AIR_B" 2 200 "$(sender_keys 460 14000)" "_b2.txt" "$WORK/_b2.out"; b2=$NODE_PID   # #2 envia "kissy"
wait_all 60 "$b1" "$b2"

q1="$WORK/_b1.out"; q2="$WORK/_b2.out"
check "#1 envia 'happy'"                     "$(has "$q1" 'Chat\] Enviando: happy')"
check "#2 envia 'kissy'"                     "$(has "$q2" 'Chat\] Enviando: kissy')"
check "#1 recibe 'kissy' (de #2)"            "$(has "$q1" 'Guardado: kissy')"
check "#2 recibe 'happy' (de #1)"            "$(has "$q2" 'Guardado: happy')"
check "#1 identifica al emisor #201"         "$(has "$q1" 'ACK a #201')"
check "#2 identifica al emisor #101"         "$(has "$q2" 'ACK a #101')"
check "#1 confirma su envio (ACK de #2)"     "$(has "$q1" 'Confirmado: entregado')"
check "#2 confirma su envio (ACK de #1)"     "$(has "$q2" 'Confirmado: entregado')"
check "half-duplex: #1 NO recibe su 'happy'" "$(hasnot "$q1" 'ACK a #101')"
check "half-duplex: #2 NO recibe su 'kissy'" "$(hasnot "$q2" 'ACK a #201')"

# ============================================================
#  ESCENA C: la hora puesta a mano en un equipo la adopta el otro (#1 -> #2).
#  #1 abre "Poner la hora" (mantener B), pone 09:06 y guarda; despues la corrige
#  HACIA ATRAS a 07:20. #2 (otro proceso, sin hora) adopta ambas por radio: la
#  segunda tiene una generacion de ajuste mas reciente aunque su hora sea anterior.
#  (Teclas: fdown/fup = B mantenida; pote -> horas pot*24/1024, decenas pot*6/1024,
#  unidades pot*10/1024.)
# ============================================================
echo
echo "==== Escena C: hora puesta a mano en #1 -> adoptada por #2 ===="
AIR_C="$WORK/air_c"
clear_air "$AIR_C"

keys_c1=$(cat <<EOF
0 pot 0
3000 fdown
4700 fup
5200 pot 400
5700 mdown
5850 mup
6300 pot 100
6800 mdown
6950 mup
7400 pot 700
7900 mdown
8050 mup
10500 pot 0
11000 fdown
12700 fup
13200 pot 300
13700 mdown
13850 mup
14300 pot 500
14800 mdown
14950 mup
15400 pot 100
15900 mdown
16050 mup
24000 q
EOF
)
start_node "$AIR_C" 1 100 "$keys_c1" "_c1.txt" "$WORK/_c1.out"; c1=$NODE_PID
start_node "$AIR_C" 2 200 "0 pot 512
30000 q" "_c2.txt" "$WORK/_c2.out"; c2=$NODE_PID
wait_all 60 "$c1" "$c2"

r1="$WORK/_c1.out"; r2="$WORK/_c2.out"
check "#1 pone 09:06 a mano"                    "$(has "$r1" 'puesta a mano: 09:06')"
check "#1 la difunde (baliza con gen 1)"        "$(has "$r1" 'hora fijada: 327[0-9]{2} gen 1')"
check "#2 adopta 09:06 de #1 (gen 1)"           "$(has "$r2" 'hora adoptada del peer: 327[0-9]{2} gen 1')"
check "#1 corrige a 07:20 (hacia atras)"        "$(has "$r1" 'puesta a mano: 07:20')"
check "#2 adopta 07:20 aunque sea anterior"     "$(has "$r2" 'hora adoptada del peer: 264[0-9]{2} gen 2')"

# --- Capturas a PNG (si hay ImageMagick) ---
conv=""
command -v magick  >/dev/null 2>&1 && conv="magick"
[[ -z "$conv" ]] && command -v convert >/dev/null 2>&1 && conv="convert"
for n in net_rx2 net_rx3; do
  if [[ -n "$conv" && -f "$WORK/$n.bmp" ]]; then "$conv" "$WORK/$n.bmp" "$WORK/$n.png" 2>/dev/null || true; fi
done

rm -f "$WORK"/_a1.txt "$WORK"/_a2.txt "$WORK"/_a3.txt "$WORK"/_b1.txt "$WORK"/_b2.txt "$WORK"/_c1.txt "$WORK"/_c2.txt
echo
if (( fail == 0 )); then echo "MULTI-DISPOSITIVO OK: comunicacion real (difusion + bidireccional) verificada."
else                     echo "$fail comprobacion(es) fallaron."; fi
exit $fail
