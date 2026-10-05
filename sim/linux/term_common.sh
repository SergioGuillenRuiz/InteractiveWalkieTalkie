# ============================================================
#  Utilidades compartidas por run.sh y net.sh (Linux / GNOME).
#  Se carga con "source"; no es ejecutable por si solo.
# ============================================================

# Tamano de ventana que necesita el render cuadrado (128x64 + estado + holgura).
SIM_TERM_COLS=132
SIM_TERM_ROWS=72

# Hay sesion grafica y GNOME Terminal disponible?
sim_have_gnome_terminal() {
  [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] && command -v gnome-terminal >/dev/null 2>&1
}

# Zoom de GNOME Terminal para que quepan SIM_TERM_ROWS filas en la pantalla:
# equivalente a la eleccion de fuente de console_win.cpp. Se puede forzar con
# la variable SIM_ZOOM (p.ej. SIM_ZOOM=0.6 ./run.sh).
sim_zoom() {
  if [[ -n "${SIM_ZOOM:-}" ]]; then echo "$SIM_ZOOM"; return; fi
  local h=""
  # Alto LOGICO del monitor (tiene en cuenta el escalado HiDPI de GNOME), via GTK.
  if command -v python3 >/dev/null 2>&1; then
    h=$(python3 - 2>/dev/null <<'PY'
import gi
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk
d = Gdk.Display.get_default()
m = d.get_primary_monitor() or d.get_monitor(0)
print(m.get_workarea().height)
PY
) || h=""
  fi
  # Respaldo: xrandr (pixeles fisicos; correcto sin escalado HiDPI).
  if [[ -z "$h" ]] && command -v xrandr >/dev/null 2>&1; then
    # Alto del monitor principal (o del primero conectado).
    h=$(xrandr --current 2>/dev/null | awk '
      / connected/ { for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+x[0-9]+\+/) {
                       split($i, a, /[x+]/);
                       if ($0 ~ / primary/) { print a[2]; exit } else if (!f) f = a[2] } }
      END { if (f) print f }' | head -1)
  fi
  [[ -z "$h" || "$h" -lt 480 ]] && h=1080
  # Celda de la fuente monoespaciada por defecto de GNOME (Adwaita Mono 11) a
  # zoom 1.0: 20 px de alto (medido con VTE). Se reservan ~100 px para la barra
  # superior de GNOME y la barra de titulo de la ventana.
  awk -v h="$h" -v r="$SIM_TERM_ROWS" 'BEGIN {
    z = (h - 100) / (r * 20.0);
    if (z > 1.0) z = 1.0; if (z < 0.3) z = 0.3;
    printf "%.2f\n", z }' 2>/dev/null || echo "0.68"
}

# Ejecuta un comando restaurando SIEMPRE el modo del terminal al terminar
# (el simulador lo pone sin eco / sin buffer de linea para leer teclas sueltas;
# si se sale con Ctrl+C conviene dejarlo como estaba).
sim_run_restoring_tty() {
  local saved=""
  if [[ -t 0 ]]; then saved="$(stty -g 2>/dev/null || true)"; fi
  local rc=0
  "$@" || rc=$?
  if [[ -n "$saved" ]]; then stty "$saved" 2>/dev/null || true; fi
  printf '\033[?25h'   # cursor visible
  return $rc
}
