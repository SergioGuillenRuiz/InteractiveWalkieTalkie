#include <Arduino.h>
#include <EEPROM.h>
#include "Frasero.h"
#include "EepromMap.h"
#include "Display.h"
#include "Inputs.h"
#include "Morse.h"      // MORSE_CANCEL_HOLD, MORSE_SEND_HOLD, MORSE_INACTIVITY
#include "TextDial.h"   // dialReset, dialResetWith, dialTick, dialMessage

// ============================================================
//  DATOS
// ============================================================

// Nombres "amables" de las 3 categorias (caben en la cabecera de 21 chars).
static const char *CAT_NAME[FR_CATEGORIES] = { "quien", "hace", "cosa" };

// Piezas por defecto (en flash). Mayusculas y sin acentos para encajar con la
// Rueda (su juego de caracteres es espacio + A..Z).
static const char *DEF_SUJETO[] = { "YO", "TU", "TE", "ME", "VAMOS A", "HOY" };
static const char *DEF_VERBO[]  = { "QUIERO", "ECHO DE MENOS", "LLEGO", "ESTOY", "VIENES", "CENAMOS" };
static const char *DEF_OBJETO[] = { "A TI", "EN CASA", "EN 10 MIN", "MUCHO", "YA", "LUEGO" };

static const char *const *DEFAULTS[FR_CATEGORIES] = { DEF_SUJETO, DEF_VERBO, DEF_OBJETO };
static const int DEF_COUNT[FR_CATEGORIES] = {
  (int)(sizeof(DEF_SUJETO) / sizeof(DEF_SUJETO[0])),
  (int)(sizeof(DEF_VERBO)  / sizeof(DEF_VERBO[0])),
  (int)(sizeof(DEF_OBJETO) / sizeof(DEF_OBJETO[0])),
};

// Piezas propias en RAM (espejo de la EEPROM). "" = slot vacio.
static String g_custom[FR_CATEGORIES][FR_SLOTS_PER_CAT];

String fraseroMessage = "";

// ============================================================
//  EEPROM (region propia tras la del historial)
// ============================================================
static const uint8_t FR_MAGIC[3] = { 'F', 'R', '1' };

static int slotAddr(int cat, int slot) {
  return EE_FRASERO_BASE + FR_HDR_SIZE + (cat * FR_SLOTS_PER_CAT + slot) * FR_SLOT_SIZE;
}

static bool magicOk() {
  for (int i = 0; i < 3; i++) if (EEPROM.read(EE_FRASERO_BASE + i) != FR_MAGIC[i]) return false;
  return true;
}

static void writeHeader() {
  for (int i = 0; i < 3; i++) EEPROM.write(EE_FRASERO_BASE + i, FR_MAGIC[i]);
  EEPROM.write(EE_FRASERO_BASE + 3, 1);   // version
}

static void writeSlotEE(int cat, int slot) {
  int addr = slotAddr(cat, slot);
  const String &s = g_custom[cat][slot];
  uint8_t len = (uint8_t)min((size_t)s.length(), (size_t)FR_PIECE_MAXLEN);
  EEPROM.write(addr, len);
  for (uint8_t i = 0; i < len; i++) EEPROM.write(addr + 1 + i, (uint8_t)s[i]);
}

static void readSlotEE(int cat, int slot) {
  int addr = slotAddr(cat, slot);
  uint8_t len = EEPROM.read(addr);
  if (len == 0 || len > FR_PIECE_MAXLEN) { g_custom[cat][slot] = ""; return; }
  String s = "";
  for (uint8_t i = 0; i < len; i++) s += (char)EEPROM.read(addr + 1 + i);
  g_custom[cat][slot] = s;
}

// Persiste todas las piezas. begin(EE_TOTAL_SIZE) para no borrar el historial.
static void saveAll() {
  EEPROM.begin(EE_TOTAL_SIZE);
  writeHeader();
  for (int c = 0; c < FR_CATEGORIES; c++)
    for (int s = 0; s < FR_SLOTS_PER_CAT; s++) writeSlotEE(c, s);
  EEPROM.commit();
  EEPROM.end();
}

void Frasero_load() {
  EEPROM.begin(EE_TOTAL_SIZE);
  if (!magicOk()) {
    // Region virgen o de formato viejo: inicializar piezas vacias.
    writeHeader();
    for (int c = 0; c < FR_CATEGORIES; c++)
      for (int s = 0; s < FR_SLOTS_PER_CAT; s++) { g_custom[c][s] = ""; EEPROM.write(slotAddr(c, s), 0); }
    EEPROM.commit();
    EEPROM.end();
    Serial.println("[Frasero] EEPROM inicializada (sin piezas propias)");
    return;
  }
  for (int c = 0; c < FR_CATEGORIES; c++)
    for (int s = 0; s < FR_SLOTS_PER_CAT; s++) readSlotEE(c, s);
  EEPROM.end();
  Serial.println("[Frasero] Piezas propias cargadas");
}

static void setCustom(int cat, int slot, const String &text) {
  String t = text;
  if ((int)t.length() > FR_PIECE_MAXLEN) t = t.substring(0, FR_PIECE_MAXLEN);
  g_custom[cat][slot] = t;
  saveAll();
  Serial.print("[Frasero] Pieza guardada ["); Serial.print(CAT_NAME[cat]);
  Serial.print(" #"); Serial.print(slot + 1); Serial.print("]: "); Serial.println(t);
}

static void clearCustom(int cat, int slot) {
  g_custom[cat][slot] = "";
  saveAll();
  Serial.print("[Frasero] Slot vaciado ["); Serial.print(CAT_NAME[cat]);
  Serial.print(" #"); Serial.print(slot + 1); Serial.println("]");
}

// ============================================================
//  RUEDA DE OPCIONES por categoria: (vacio) + propias llenas + defaults
// ============================================================
static int filledCustoms(int cat) {
  int n = 0;
  for (int s = 0; s < FR_SLOTS_PER_CAT; s++) if (g_custom[cat][s].length() > 0) n++;
  return n;
}

static int optionCount(int cat) { return 1 + filledCustoms(cat) + DEF_COUNT[cat]; }

// idx 0 = (vacio); luego las propias llenas (en orden de slot); luego las default.
static String optionText(int cat, int idx) {
  if (idx <= 0) return "";
  int k = idx - 1;
  for (int s = 0; s < FR_SLOTS_PER_CAT; s++) {
    if (g_custom[cat][s].length() > 0) {
      if (k == 0) return g_custom[cat][s];
      k--;
    }
  }
  if (k < DEF_COUNT[cat]) return String(DEFAULTS[cat][k]);
  return "";
}

// ============================================================
//  MAQUINA DE ESTADOS
// ============================================================
enum FrMode { FRM_COMPOSE, FRM_MANAGE, FRM_EDIT };

static FrMode        s_mode = FRM_COMPOSE;
static bool          s_init = false;
static bool          s_drawForce = true;

static int           s_pos = 0;                  // categoria activa (compose)
static int           s_sel[FR_CATEGORIES];       // opcion elegida por categoria
static int           s_manageSlot = 0;           // slot resaltado en gestion
static bool          s_manageLock = false;       // ignora el pote hasta que el usuario lo gire
static int           s_manageLockVal = 0;        // valor del pote al entrar a gestion

// Seleccion estable con el pote (compose y manage comparten el candidato).
static int           s_cand = -1;
static unsigned long s_candSince = 0;
static unsigned long s_lastInput = 0;
static const unsigned long SEL_DEBOUNCE_MS = 120;

// Flancos de botones (solo compose/manage; en edit manda dialTick).
static unsigned long s_mStart = 0, s_fStart = 0;
static bool          s_mWas = false, s_fWas = false, s_mLong = false, s_fLong = false;

static String composeMessage() {
  String out = "";
  for (int c = 0; c < FR_CATEGORIES; c++) {
    String p = optionText(c, s_sel[c]);
    if (p.length() == 0) continue;
    if (out.length() > 0) out += " ";
    out += p;
  }
  return out;
}

void fraseroReset() {
  s_mode = FRM_COMPOSE;
  s_init = false;
}

// ---------- dibujo ----------
static void drawWrap(const String &msg, int startY, int maxLines) {
  const int MAXC = 21;
  int y = startY;
  String rem = msg;
  int ln = 0;
  while (rem.length() > 0 && ln < maxLines) {
    int cut = (int)rem.length();
    if ((int)rem.length() > MAXC) {
      cut = MAXC;
      for (int i = cut; i > 0; i--) if (rem.charAt(i) == ' ') { cut = i; break; }
    }
    display.setCursor(0, y);
    display.print(rem.substring(0, cut));
    rem = rem.substring(cut);
    if (rem.length() > 0 && rem.charAt(0) == ' ') rem = rem.substring(1);
    y += 10;
    ln++;
  }
}

static String fitLine(String v, int maxChars) {
  if ((int)v.length() > maxChars) v = v.substring(0, maxChars - 1) + ".";
  return v;
}

static void drawCompose() {
  bool force = s_drawForce; s_drawForce = false;
  static String last = "\x01";
  String key = String(s_pos);
  for (int c = 0; c < FR_CATEGORIES; c++) { key += "|"; key += optionText(c, s_sel[c]); }
  if (!force && key == last) return;
  last = key;

  display.clearDisplay();
  drawTitleBar("Frase");
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  int y = 18;
  for (int c = 0; c < FR_CATEGORIES; c++) {
    String v = optionText(c, s_sel[c]);
    if (v.length() == 0) v = "(vacio)";
    drawListRow(y, String(CAT_NAME[c]) + ": " + fitLine(v, 11), c == s_pos);
    y += 13;
  }

  display.drawFastHLine(0, 59, 128, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  String prev = composeMessage();
  if (prev.length() == 0) prev = "(vacio)";
  drawWrap(prev, 64, 3);

  display.setCursor(0, 108);
  display.print("A:fija/avanza B:atras");
  display.setCursor(0, 118);
  display.print("manten A: piezas");
  display.display();
}

static void drawManage() {
  bool force = s_drawForce; s_drawForce = false;
  static String last = "\x01";
  String key = String(s_manageSlot) + ":" + String(s_pos);
  for (int s = 0; s < FR_SLOTS_PER_CAT; s++) { key += "|"; key += g_custom[s_pos][s]; }
  if (!force && key == last) return;
  last = key;

  display.clearDisplay();
  drawTitleBar("Mis palabras", CAT_NAME[s_pos]);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  int y = 18;
  for (int s = 0; s < FR_SLOTS_PER_CAT; s++) {
    display.setCursor(0, y);
    display.print(s == s_manageSlot ? ">" : " ");
    display.print(s + 1);
    display.print(" ");
    String v = g_custom[s_pos][s];
    if (v.length() == 0) v = "(vacio)";
    display.print(fitLine(v, 16));
    y += 12;
  }

  display.setCursor(0, 80);  display.print("A: crear/editar");
  display.setCursor(0, 92);  display.print("B: vaciar");
  display.setCursor(0, 104); display.print("manten B: volver");
  display.display();
}

// Tras volver de la Rueda: realinear el detector de flancos al estado fisico
// actual para no procesar un flanco fantasma en el primer tick.
static void syncButtons() {
  s_mWas = isMorsePressed();
  s_fWas = isFinishPressed();
  s_mLong = s_fLong = false;
}

// ---------- tick ----------
FraseroResult fraseroTick() {
  unsigned long now = millis();

  if (!s_init) {
    s_init = true;
    s_mode = FRM_COMPOSE;
    s_pos = 0;
    for (int c = 0; c < FR_CATEGORIES; c++) s_sel[c] = (optionCount(c) > 1) ? 1 : 0;
    s_cand = -1; s_candSince = now; s_lastInput = now;
    s_manageSlot = 0; s_manageLock = false;
    s_mWas = s_fWas = false; s_mLong = s_fLong = false;
    s_drawForce = true;
  }

  // --- EDIT: la Rueda (dialTick) gestiona pote y botones por completo ---
  if (s_mode == FRM_EDIT) {
    DialResult r = dialTick();
    if (r == DIAL_SENT) {
      setCustom(s_pos, s_manageSlot, dialMessage);
      s_mode = FRM_MANAGE; s_cand = -1; s_lastInput = now; s_drawForce = true; syncButtons();
    } else if (r == DIAL_CANCELLED) {
      s_mode = FRM_MANAGE; s_cand = -1; s_lastInput = now; s_drawForce = true; syncButtons();
    }
    return FR_NONE;
  }

  // --- Deteccion de flancos (compose / manage) ---
  bool mNow = isMorsePressed();
  bool fNow = isFinishPressed();
  if (mNow && !s_mWas) { s_mStart = now; s_mLong = false; }
  if (fNow && !s_fWas) { s_fStart = now; s_fLong = false; }
  if (mNow && !s_mLong && now - s_mStart >= MORSE_CANCEL_HOLD) s_mLong = true;
  if (fNow && !s_fLong && now - s_fStart >= MORSE_SEND_HOLD)   s_fLong = true;
  bool mShort    = (!mNow && s_mWas) && !s_mLong;
  bool mLongFire = (!mNow && s_mWas) &&  s_mLong;
  bool fShort    = (!fNow && s_fWas) && !s_fLong;
  bool fLongFire = (!fNow && s_fWas) &&  s_fLong;
  s_mWas = mNow; s_fWas = fNow;

  if (s_mode == FRM_COMPOSE) {
    int maxIdx = optionCount(s_pos) - 1;
    int mapped = getPotValue(maxIdx);
    if (mapped != s_cand) { s_cand = mapped; s_candSince = now; }
    else if (now - s_candSince >= SEL_DEBOUNCE_MS && s_sel[s_pos] != s_cand) {
      s_sel[s_pos] = s_cand; s_lastInput = now;
    }

    drawCompose();

    if (mLongFire) {   // gestionar piezas de la categoria activa
      // Arrancar siempre en el primer slot; el pote (que viene de la posicion de
      // compose) no debe mover el cursor hasta que el usuario lo gire a proposito.
      s_mode = FRM_MANAGE; s_manageSlot = 0; s_cand = -1; s_lastInput = now; s_drawForce = true;
      s_manageLock = true; s_manageLockVal = getPotValue(FR_SLOTS_PER_CAT - 1);
      return FR_NONE;
    }
    if (mShort) {      // fija y avanza
      s_lastInput = now;
      if (s_pos < FR_CATEGORIES - 1) { s_pos++; s_cand = -1; s_drawForce = true; }
      else {
        fraseroMessage = composeMessage();
        if (fraseroMessage.length() > 0) return FR_SENT;
      }
      return FR_NONE;
    }
    if (fLongFire) {   // enviar ya
      fraseroMessage = composeMessage();
      s_lastInput = now;
      if (fraseroMessage.length() > 0) return FR_SENT;
      return FR_NONE;
    }
    if (fShort) {      // retrocede / salir
      s_lastInput = now;
      if (s_pos > 0) { s_pos--; s_cand = -1; s_drawForce = true; }
      else return FR_EXIT;
      return FR_NONE;
    }
    if (now - s_lastInput > MORSE_INACTIVITY) return FR_EXIT;
    return FR_NONE;
  }

  // FRM_MANAGE
  {
    int mapped = getPotValue(FR_SLOTS_PER_CAT - 1);
    if (s_manageLock) {
      // Mantener el cursor en el primer slot hasta detectar un giro real del pote.
      if (mapped != s_manageLockVal) { s_manageLock = false; s_cand = -1; }
    }
    if (!s_manageLock) {
      if (mapped != s_cand) { s_cand = mapped; s_candSince = now; }
      else if (now - s_candSince >= SEL_DEBOUNCE_MS && s_manageSlot != s_cand) {
        s_manageSlot = s_cand; s_lastInput = now; s_drawForce = true;
      }
    }

    drawManage();

    if (mShort) {      // crear (si vacio) o editar (si lleno) -> abre la Rueda
      s_mode = FRM_EDIT;
      if (g_custom[s_pos][s_manageSlot].length() > 0) dialResetWith(g_custom[s_pos][s_manageSlot]);
      else dialReset();
      dialSetFinishLabel("guardar");      // aqui la Rueda guarda la pieza, no envia
      dialSetMaxLen(FR_PIECE_MAXLEN);     // no permitir escribir mas de lo que cabe
      s_lastInput = now;
      return FR_NONE;
    }
    if (fShort) {      // vaciar el slot
      if (g_custom[s_pos][s_manageSlot].length() > 0) clearCustom(s_pos, s_manageSlot);
      s_lastInput = now; s_drawForce = true;
      return FR_NONE;
    }
    if (fLongFire) {   // volver a componer
      // por si cambio el numero de opciones, recolocar la seleccion
      if (s_sel[s_pos] >= optionCount(s_pos)) s_sel[s_pos] = optionCount(s_pos) - 1;
      s_mode = FRM_COMPOSE; s_cand = -1; s_lastInput = now; s_drawForce = true;
      return FR_NONE;
    }
    if (now - s_lastInput > MORSE_INACTIVITY) return FR_EXIT;
    return FR_NONE;
  }
}
