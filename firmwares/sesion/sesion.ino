// ==============================================================================================================================================
// PERCU-SYNTH — SESION (bajo + acordes + cuerdas resonantes + resonador, todo coordinado) — GC Lab Chile
// ==============================================================================================================================================
// HARDWARE: ESP32-S3 (PSRAM OPI) + DAC PCM5102 (I2S: LCK 39, DIN 40, BCK 41) + 5 botones + 4 pots + 6 LEDs SMD WS2812 + OLED SSD1306 (SDA 21, SCL 38)
//           + microSD (SCK 14, MOSI 15, MISO 16, CS 17; FAT32)
// ARDUINO IDE: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · PSRAM: OPI · optimización -O2 · librería FastLED (sólo para CRGB/CHSV)
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// Una sesión pequeña pensada como Ableton Live 12: 4 pistas (BAJO · ACORDES · MELODIA · RESONADOR) y una pista maestra (SESION) que fija el tempo,
// la escala y la tónica GLOBALES; todo lo que tiene altura se calcula en grados de esa escala, por eso todo está en tono. Las pistas comparten un
// reloj maestro de 16 pasos por compás (con swing) y cada pista elige su rejilla (1/4 · 1/8 · 1/16).
//   · Lo que pides con los botones NO suena en ese instante: queda "ARMADO" y entra en el siguiente compás (cuantización de lanzamiento), y cada
//     cambio de voz pasa por un cruce corto: pulsar un botón nunca hace clic, golpe ni ruido.
//   · 8 ESCENAS (escala + progresión + patrón de bajo + voicing + sonido + modo del resonador); en AUTO ("follow action") pasa sola a otra cada 16 compases.
//   · Retornos como en Live: reverb de placa y eco sincronizado (corchea con puntillo) con un solo mando (ENVIOS), y un auto-nivel + limitador al final.
//   · Sonoro y resonante: la melodía son cuerdas de Karplus-Strong afinadas a la escala (resuenan solas) y el RESONADOR es un ensamble de filtros de peine
//     afinado al acorde (como el dispositivo Resonators), que hace vibrar por simpatía lo que suena.
// REGLAS FIJAS: BTN1 corto = siguiente pista · mantenido 0,7 s = menú de escenas · POT1 volumen · BTN2 stop/play de la pista · POT2 tempo/rejilla ·
//   todo se guarda solo en la microSD (/SESION/escena.txt) · la pantalla muestra pista, valores, función de cada botón y qué está armado.
// ==============================================================================================================================================

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <FastLED.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <SD.h>
#include <SPI.h>

#ifndef DIAG
#define DIAG 0
#endif
#ifndef OLED_CORE
#define OLED_CORE 0
#endif
#if DIAG
#define DBG_PRINTF(...)  do { Serial0.printf(__VA_ARGS__); Serial.printf(__VA_ARGS__); } while (0)
#define DBG_PRINTLN(x)   do { Serial0.println(x); Serial.println(x); } while (0)
#endif

struct Coef { float a1, a2, a3, k; };                // SVF TPT: coeficientes
struct SvfS { float ic1, ic2; };                      // SVF TPT: estado
struct Rampa { Coef cur, paso; };                     // coeficientes que se interpolan por muestra

#define I2S_LCK   39
#define I2S_DIN   40
#define I2S_BCK   41
#define SAMPLE_RATE     48000                         // 48 kHz / 24 bit (como Z)
#define BUFFER_SAMPLES  128
const float SR     = (float)SAMPLE_RATE;
const float INV_SR = 1.0f / (float)SAMPLE_RATE;
const uint32_t BUF_US = (uint32_t)(1.0e6f * BUFFER_SAMPLES / SAMPLE_RATE);      // 2667 µs
const float INC_K = 4294967296.0f / (float)SAMPLE_RATE;

#define LED_PIN        46
#define NUM_LEDS        6
#define LED_BRIGHT     160
#define ONBOARD_PIN    48
#define ONBOARD_BRIGHT 40
const unsigned long LED_REFRESH_MS = 33;
CRGB leds[NUM_LEDS];
CRGB onboard[1];

#define BTN1_PIN   44
#define BTN2_PIN   42
#define BTN3_PIN    0
#define BTN4_PIN   45
#define BTN5_PIN   47
const unsigned long DEBOUNCE_MS = 40;
#define POT1   1
#define POT2   2
#define POT3   8
#define POT4  10


// ==============================================================================================================================================
// UTILIDADES DSP (todas baratas por muestra)
// ==============================================================================================================================================
float SIN[1024 + 1];
inline float sinPh(uint32_t ph) {                                   // seno por tabla con interpolación; fase entera de 32 bits
  uint32_t i = ph >> 22;
  float f = (float)(ph & 0x3FFFFF) * (1.0f / 4194304.0f);
  float a = SIN[i];
  return a + (SIN[i + 1] - a) * f;
}
inline float tabPh(const float *t, uint32_t ph) {
  uint32_t i = ph >> 22;
  float f = (float)(ph & 0x3FFFFF) * (1.0f / 4194304.0f);
  float a = t[i];
  return a + (t[i + 1] - a) * f;
}
inline uint32_t incF(float hz) { return (uint32_t)(hz * INC_K); }

uint32_t rngS = 2463534242u;
inline uint32_t xr() { rngS ^= rngS << 13; rngS ^= rngS >> 17; rngS ^= rngS << 5; return rngS; }
inline float ruido() { return (float)(int32_t)xr() * (1.0f / 2147483648.0f); }
inline float rnd01() { return (float)(xr() >> 8) * (1.0f / 16777216.0f); }

inline float softClip(float x) {
  if (x >  3.0f) x =  3.0f;
  if (x < -3.0f) x = -3.0f;
  return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
inline float sat3(float x) {                                         // saturación cúbica suave, sin división: x − x³/3, tope en ±2/3
  if (x >  1.0f) return  0.66667f;
  if (x < -1.0f) return -0.66667f;
  return x - x * x * x * (1.0f / 3.0f);
}
inline float limitador(float x) {
  float a = fabsf(x);
  if (a < 0.75f) return x;
  float t = (a - 0.75f) * 4.0f; if (t > 3.0f) t = 3.0f;
  float o = 0.75f + 0.25f * softClip(t);
  return x < 0.0f ? -o : o;
}
inline Coef coefDe(float g, float k) {
  Coef c; c.k = k;
  c.a1 = 1.0f / (1.0f + g * (g + k));
  c.a2 = g * c.a1;
  c.a3 = g * c.a2;
  return c;
}
inline float gDe(float fc) {                                        // g = tan(π·fc/SR)
  if (fc > 0.49f * SR) fc = 0.49f * SR;
  if (fc < 10.0f) fc = 10.0f;
  return tanf((float)M_PI * fc * INV_SR);
}
inline void rampaHacia(Rampa &r, const Coef &t, float inv) {         // inv = 1 / (muestras del bloque)
  r.paso.a1 = (t.a1 - r.cur.a1) * inv; r.paso.a2 = (t.a2 - r.cur.a2) * inv; r.paso.a3 = (t.a3 - r.cur.a3) * inv; r.paso.k = (t.k - r.cur.k) * inv;
}
inline void rampaAvanza(Rampa &r) { r.cur.a1 += r.paso.a1; r.cur.a2 += r.paso.a2; r.cur.a3 += r.paso.a3; r.cur.k += r.paso.k; }
inline float svfLP(SvfS &s, const Coef &c, float in) {
  float v3 = in - s.ic2;
  float v1 = c.a1 * s.ic1 + c.a2 * v3;
  float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
  s.ic1 = 2.0f * v1 - s.ic1;
  s.ic2 = 2.0f * v2 - s.ic2;
  return v2;
}
inline float svfBP(SvfS &s, const Coef &c, float in) {
  float v3 = in - s.ic2;
  float v1 = c.a1 * s.ic1 + c.a2 * v3;
  float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
  s.ic1 = 2.0f * v1 - s.ic1;
  s.ic2 = 2.0f * v2 - s.ic2;
  return c.k * v1;
}

// ─── Tablas de sierra limitada en banda (6 octavas, 1024 puntos); se generan al arrancar ───
#define TBL_N 1024
#define NMIP  6
const float MIP_F0 = 27.5f;
int16_t tabSaw[NMIP][TBL_N];
void construirTablas() {
  float amp[260], tmp[TBL_N];
  for (int m = 0; m < NMIP; m++) {
    float fhi = MIP_F0 * (float)(1 << (m + 1));
    int H = (int)(14000.0f / fhi); if (H > 255) H = 255; if (H < 1) H = 1;
    for (int n = 1; n <= H; n++) {
      float x = (float)n / (float)(H + 1);
      amp[n] = (1.0f / (float)n) * sinf((float)M_PI * x) / ((float)M_PI * x);       // Lanczos
    }
    float pico = 1e-9f;
    for (int i = 0; i < TBL_N; i++) {
      float th = 2.0f * (float)M_PI * (float)i / (float)TBL_N;
      float s1 = sinf(th), c1 = cosf(th), sn = s1, cn = c1, acc = 0.0f;
      for (int n = 1; n <= H; n++) { acc += amp[n] * sn; float s2 = sn * c1 + cn * s1; cn = cn * c1 - sn * s1; sn = s2; }
      tmp[i] = acc; if (fabsf(acc) > pico) pico = fabsf(acc);
    }
    for (int i = 0; i < TBL_N; i++) tabSaw[m][i] = (int16_t)lrintf(tmp[i] / pico * 30000.0f);
  }
}
inline int mipDe(float f) { int m = 0; float lim = MIP_F0 * 2.0f; while (m < NMIP - 1 && f >= lim) { lim *= 2.0f; m++; } return m; }
inline float leeTabla(const int16_t *t, uint32_t ph) {
  uint32_t i = ph >> 22;
  float f = (float)(ph & 0x3FFFFF) * (1.0f / 4194304.0f);
  float a = (float)t[i], b = (float)t[(i + 1) & (TBL_N - 1)];
  return (a + (b - a) * f) * (1.0f / 32768.0f);
}
float SUBW[1025];                                                     // sub: seno con un 2º armónico suave



// ─── rack ───
#define ECO_N 32768
int16_t *ecoL = 0, *ecoR = 0;
int ecoW = 0, ecoDA = 20000, ecoDB = 20000; float ecoX = 0;
const float ECO_XF = 1.0f / (0.040f * SAMPLE_RATE);
float ecoLpL = 0, ecoLpR = 0, eHpL = 0, eHpR = 0; const float ECO_HP = 0.0128f;
float ecoFb = 0.35f;

// ─── placa de Dattorro (a media tasa) ───
float ia1[142], ia2[107], ia3[379], ia4[277];
int ii1 = 0, ii2 = 0, ii3 = 0, ii4 = 0;
float lAp1[672 + 40], lD1[4453], lAp2[1800], lD2[3720];
float rAp1[908 + 40], rD1[4217], rAp2[2656], rD2[3163];
int lAp1w = 0, lD1w = 0, lAp2w = 0, lD2w = 0, rAp1w = 0, rD1w = 0, rAp2w = 0, rD2w = 0;
float lpL = 0, lpR = 0, bwIn = 0, fbL = 0, fbR = 0, xAcc = 0, pLo = 0, pRo = 0, pLn = 0, pRn = 0;
uint32_t pFase = 0;
uint32_t lfo1 = 0, lfo2 = 0x4CCCCCCCu;
const uint32_t lfo1Inc = (uint32_t)(0.55f * 4294967296.0f / (float)SAMPLE_RATE), lfo2Inc = (uint32_t)(0.73f * 4294967296.0f / (float)SAMPLE_RATE);

inline float apFijo(float *buf, int n, int &w, float x, float g) {
  float d = buf[w];
  float v = x + g * d;
  buf[w] = v;
  if (++w >= n) w = 0;
  return d - g * v;
}
inline float apMod(float *buf, int n, int &w, float x, float g, float retardo) {
  float rp = (float)w - retardo; if (rp < 0.0f) rp += (float)n;
  int i0 = (int)rp; float fr = rp - (float)i0;
  int i1 = i0 + 1; if (i1 >= n) i1 = 0;
  float d = buf[i0] + (buf[i1] - buf[i0]) * fr;
  float v = x + g * d;
  buf[w] = v;
  if (++w >= n) w = 0;
  return d - g * v;
}
inline float tomar(const float *buf, int n, int w, int d) { int r = w - d; if (r < 0) r += n; return buf[r]; }



// ==============================================================================================================================================
// ARMONÍA GLOBAL (como la "escala global" de Live 12): UNA tónica y UNA escala; todo lo que tiene altura se calcula en grados de esa escala
// ==============================================================================================================================================
#define NESC 8
static const char *ESC_NOM[NESC] = { "DORICO", "LIDIO", "EOLICO", "MIXOLIDIO", "PENTA MENOR", "PENTA MAYOR", "HIRAJOSHI", "INSEN" };
static const char *ESC_COR[NESC] = { "DORICO", "LIDIO", "EOLICO", "MIXOL", "PENT-", "PENT+", "HIRAJ", "INSEN" };
const uint8_t ESC_N[NESC] = { 7, 7, 7, 7, 5, 5, 5, 5 };
const int8_t ESC_I[NESC][7] = {
  { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 10 },
  { 0, 3, 5, 7, 10, 0, 0 }, { 0, 2, 4, 7, 9, 0, 0 }, { 0, 2, 3, 7, 8, 0, 0 }, { 0, 1, 5, 7, 10, 0, 0 } };
static const char *TON_NOM[12] = { "DO", "DO#", "RE", "MIB", "MI", "FA", "FA#", "SOL", "LAB", "LA", "SIB", "SI" };

// ─── capas = pistas de una sesión ───
#define NCAPAS 7                                                      // 0 BAJO · 1 ACORDES · 2 MELODIA · 3 RESONADOR · 4 SESION (maestro) · 5 TEXTURA (granular) · 6 MATRIZ (modulación)
volatile float par[NCAPAS][4];                                        // control → audio: valores 0..1 de las 4 perillas de cada capa
volatile uint8_t runMask = 0x3F;                                      // bit n = la pista n suena (BTN2): 0-3 pistas · 4 textura · 5 matriz de modulación
// Peticiones del control. NADA cambia al instante: el audio las aplica en el siguiente compás (como la cuantización de lanzamiento de Live),
// y todo cambio de voz pasa por un cruce corto → pulsar un botón nunca hace clic ni golpe.
volatile uint8_t reqBTim = 0; volatile uint8_t reqEsc = 0, reqTon = 2, reqEscena = 0, reqBPat = 0, reqBOct = 1, reqGlide = 0, reqProg = 0, reqVoic = 0, reqPadRit = 0;
volatile uint8_t reqMSon = 0, reqMOct = 1, reqModo = 0, reqAlt = 1, reqFrz = 0, reqAuto = 1; volatile uint32_t reqMGen = 0;
volatile uint8_t reqPoli = 0, reqGSize = 1, reqGArm = 1, reqGFrz = 0;                  // polimetría · TEXTURA (tamaño de grano, armonía, congelar)
float mAmt[4][7]; volatile uint8_t mSrcSel = 0, mDstSel = 0, lfoShape[2] = { 0, 1 }, msegSel = 0;   // matriz de modulación: cantidad por (fuente, destino)
volatile uint8_t g_esc = 0, g_ton = 2, g_escena = 0, g_pend = 0, g_paso = 0, g_acorde = 0;
volatile float g_pul[NCAPAS] = { 0, 0, 0, 0, 0, 0, 0 }, g_niv[NCAPAS] = { 0, 0, 0, 0, 0, 0, 0 };
volatile bool g_sinRecoger = false;
volatile int g_sd = 0;
volatile unsigned long xrunAt = 0;
volatile uint32_t g_usAvg = 0, g_usMax = 0, g_xruns = 0, g_heal = 0;
volatile float g_bpm = 92;
#if DIAG
float dgPk = 0; uint32_t dgSobre = 0; unsigned long dgCtlMax = 0;
#endif

static const char *BPAT[8] = { "1---1---1---1---", "1--1--1---1--1--", "1-1-1-1-1-1-1-1-", "1---------1-----", "1--1----1--1-1--", "1-1--1-1-1--1-1-", "1---1--1--1-1---", "1-----1---1--1-2" };
static const char *BPAT_NOM[8] = { "4X4", "TRESILLO", "OCTAVOS", "PEDAL", "ROTO", "SINCOPA", "2-STEP", "UKG" };
static const char *BTIM_NOM[3] = { "SUB", "ACIDO", "REESE" };
static const int8_t PROGS[6][4] = { { 0, 3, 4, 3 }, { 0, 4, 5, 3 }, { 0, 2, 3, 4 }, { 0, 5, 3, 4 }, { 0, 1, 4, 0 }, { 0, 3, 0, 4 } };
static const char *VOIC_NOM[5] = { "TRIADA", "ABIERTO", "SUS2", "SUS4", "SEPTIMA" };
static const char *MSON_NOM[5] = { "ARPA", "CUERDA", "MARIMBA", "VIDRIO", "CAMPANA" };      // 4 = campana FM
static const char *POLI_NOM[8] = { "16/16", "16/12", "16/10", "12/16", "14/16", "16/20", "12/10", "20/12" };
static const uint8_t POLI[8][2] = { { 16, 16 }, { 16, 12 }, { 16, 10 }, { 12, 16 }, { 14, 16 }, { 16, 20 }, { 12, 10 }, { 20, 12 } };   // largo de patrón de BAJO / MELODIA (polimetría sobre el reloj maestro)
static const char *GSIZE_NOM[4] = { "10MS", "25MS", "50MS", "100MS" };
static const char *GARM_NOM[4] = { "UNISONO", "ACORDE", "OCTAVAS", "QUINTAS" };
static const char *SRC_NOM[4] = { "LFO1", "LFO2", "MSEG", "ENV" };
static const char *DST_NOM[7] = { "CORTE B", "CORTE A", "TABLA", "BRILLO", "DECAIM", "ENVIOS", "PITCH M" };
static const char *LFO_NOM[4] = { "SENO", "TRIANG", "SALTO", "RAMPA" };
static const char *MSEG_NOM[8] = { "PERCUSION", "BOMBEO", "RAMPA", "ESCALERA", "RESPIRA", "TRES", "CAIDA", "EVOLUTIVO" };
static const char *MODO_NOM[4] = { "ACORDE", "QUINTAS", "OCTAVAS", "ESCALA" };
static const char *PRIT_NOM[3] = { "SOSTENIDO", "PULSADO", "STAB DUB" };
static const char *STABP = "1--1--1---1---1-";
struct Escena { const char *nom; uint8_t esc, prog, bpat, voic, msn, modo, prit, btim; };
const Escena ESCENAS[8] = {                                           // inspiradas en el mundo Dekmantel: techno experimental, club roto, dub, acid, ambiente, sub-bajo
  { "UFO", 2, 2, 5, 3, 4, 2, 0, 1 }, { "BROKEN", 0, 1, 6, 2, 2, 0, 2, 0 }, { "DUB", 4, 0, 3, 3, 1, 1, 2, 2 }, { "ACIDO", 7, 4, 2, 1, 0, 3, 1, 1 },
  { "AMBIENTE", 1, 1, 3, 0, 3, 1, 0, 0 }, { "SUB", 2, 3, 7, 0, 4, 2, 0, 2 }, { "ARP", 3, 0, 4, 4, 0, 3, 1, 0 }, { "NIEBLA", 6, 5, 0, 2, 3, 0, 0, 0 } };

// estado aplicado (sólo el audio lo toca)
int bTim = 0, poli = 0, esc = 0, ton = 2, escena = 0, bPat = 0, bOct = 1, glideOn = 0, prog = 0, voic = 0, padRit = 0, mSon = 0, mOct = 1, modo = 0, alt = 1, frz = 0;
int grado = 0, chordIdx = 0; uint32_t barN = 0;
inline int midiEsc(int idx, int base) {
  int n = ESC_N[esc];
  int oc = idx >= 0 ? idx / n : -((-idx + n - 1) / n);
  int r = idx - oc * n;
  return base + 12 * oc + ESC_I[esc][r];
}
inline int tonBase() { return 60 + ((ton + 6) % 12) - 6; }
inline float noteHz(int m) { return 440.0f * powf(2.0f, ((float)m - 69.0f) * (1.0f / 12.0f)); }
inline int cuantEsc(int m) {                                          // fuerza cualquier nota a la escala activa (la más cercana)
  int rel = ((m - ton) % 12 + 12) % 12, base = m - rel, best = 0, bd = 99;
  for (int k = 0; k < ESC_N[esc]; k++) { int d = abs(ESC_I[esc][k] - rel); if (d < bd) { bd = d; best = ESC_I[esc][k]; } }
  { int d = 12 - rel; if (d < bd) { bd = d; best = 12; } }
  return base + best;
}
inline bool esTonoAcorde(int idx) { int n = ESC_N[esc]; int d = ((idx - grado) % n + n) % n; return d == 0 || d == 2 % n || d == 4 % n; }
inline float cl01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float rrange(float a, float b) { return a + (b - a) * rnd01(); }

// ==============================================================================================================================================
// VOCES
// ==============================================================================================================================================
// BAJO: dos voces que se alternan (la que sale se apaga mientras entra la nueva: sin cortes) · sub seno + sierra · filtro resonante
struct VBajo { uint32_t ph, ph2, phS; float inc, incT, amp, ampT, fenv; int mip; SvfS f; Rampa r; };
VBajo bv[2]; int bCur = 0; int bGateSteps = 0;
// ACORDES: 2 juegos × 4 notas × 2 sierras desafinadas; al cambiar de acorde un juego sale y el otro entra
struct SlotP { uint32_t ph[2], inc[2]; float g, gT; int mip; float pl, pr, hz, dr, drT, tim, timT; };   // dr/tim = expresión POR NOTA (MPE): desafinación y timbre propios
SlotP pad[2][4]; int padSet = 0, padN = 3; float padGate = 1, padGateT = 1;
SvfS padFL, padFR; Rampa padR;
// MELODIA: cuerdas de Karplus-Strong afinadas a la escala (resuenan solas)
#define KS_N 1024
#define NKS 6
struct VKS { float buf[KS_N]; int w; float D, c, g, lp, env, gL, gR; bool on; };
VKS ks[NKS]; bool melHit[16]; int8_t melOff[16]; int mIdx = 0; float melDens = 0.5f;
// RESONADOR: dos bancos de 4 filtros de peine afinados al acorde (como el dispositivo Resonators de Live), cruzados al cambiar de acorde
#define NCB 4
struct Peine { float buf[KS_N]; int w; float D, lp; };
Peine pb[2][NCB]; int rbSet = 0; float rbG[2] = { 1, 0 }, rbFb = 0.9f, rbIn = 0.5f, rbDamp = 0.5f;
// TABLA DE ONDAS (8 cuadros: seno → triángulo → sierra → cuadrada → sierra rica → formante A → formante O → brillante); la posición la mueve la matriz
float WT[8][1025], WTcur[1025]; float wtPosApl = -1.0f;
// CAMPANA FM: portadora + moduladora inarmónica (razón 3,5 > 2: las bandas laterales no caen debajo de la fundamental)
struct VFM { uint32_t pc, pm; float ic, im, amp, idx, ad, xd; bool on; float gL, gR; };
#define NFM 4
VFM fm[NFM];
// TEXTURA: granulador sobre lo que suena (remuestreo propio), granos afinados a la escala/acorde
#define G2_N 32768
int16_t *gBuf2 = 0; int gW2 = 0, gCnt2 = 1; float gLpL = 0, gLpR = 0;
struct Grano2 { float pos, rate, amp, gL, gR; int len, age; bool on; };
Grano2 gr2[6];
// MSEG: curvas de varias etapas (puntos tiempo/nivel) sincronizadas al compás
struct Punto { float t, v; };
static const Punto MSEG[8][6] = {
  { { 0, 1 }, { 0.12f, 0.35f }, { 0.4f, 0.08f }, { 1, 0 }, { 1, 0 }, { 1, 0 } },                       // PERCUSION
  { { 0, 0.2f }, { 0.25f, 1 }, { 0.5f, 0.2f }, { 0.75f, 1 }, { 1, 0.2f }, { 1, 0.2f } },               // BOMBEO
  { { 0, 0 }, { 0.9f, 1 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } },                                  // RAMPA
  { { 0, 0.1f }, { 0.25f, 0.1f }, { 0.26f, 0.4f }, { 0.5f, 0.4f }, { 0.51f, 1 }, { 1, 1 } },           // ESCALERA
  { { 0, 0 }, { 0.5f, 1 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } },                                  // RESPIRA
  { { 0, 1 }, { 0.1f, 0.1f }, { 0.34f, 1 }, { 0.44f, 0.1f }, { 0.67f, 1 }, { 1, 0.1f } },             // TRES (3 pulsos por vuelta)
  { { 0, 1 }, { 0.5f, 0.2f }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } },                               // CAIDA
  { { 0, 0.5f }, { 0.2f, 0.9f }, { 0.4f, 0.3f }, { 0.6f, 0.7f }, { 0.8f, 0.2f }, { 1, 0.5f } } };      // EVOLUTIVO
float lfoPh[2] = { 0, 0 }, lfoSH[2] = { 0, 0 }, srcV[4] = { 0, 0, 0, 0 };
// MIDI de salida con MPE (cada nota del acorde en su propio canal, con inflexión y timbre propios)
#ifndef MIDI_BAUD
#define MIDI_BAUD 115200
#endif
uint8_t midiQ[512]; volatile uint16_t mqH = 0, mqT = 0;
int padMidi[4] = { -1, -1, -1, -1 }; int bassMidi = -1, melMidi = -1;
volatile float g_dr[4] = { 0, 0, 0, 0 }, g_tim[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
inline void midi3(uint8_t st, uint8_t a, uint8_t b) {                 // el mensaje de 3 bytes se publica de una vez (el control nunca ve medio mensaje)
  uint16_t h = mqH; if ((uint16_t)((h + 3) & 511) == mqT || (uint16_t)((h + 2) & 511) == mqT || (uint16_t)((h + 1) & 511) == mqT) return;
  midiQ[h] = st; midiQ[(h + 1) & 511] = a; midiQ[(h + 2) & 511] = b; mqH = (h + 3) & 511;
}
// reloj maestro
int stepCnt = 1, stepPos = 0; float stepBase = 6000; int lenB = 16, lenM = 16, cB = 0, cM = 0;
float runG[5] = { 1, 1, 1, 1, 1 };
float vS[NCAPAS] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
float vMs = 0.8f, revS = 0.3f, ecoS = 0.3f; float cmpEnv = 0, cmpGan = 1, cmpGT = 1, lpOL = 0, lpOR = 0, hpL = 0, hpR = 0, duck = 0;
static i2s_chan_handle_t tx_chan;
float mPrevB = 0, padAk = 0.01f, melBr = 0.5f, modPitch = 0;

void resetAudio() {
  memset(ia1, 0, sizeof(ia1)); memset(ia2, 0, sizeof(ia2)); memset(ia3, 0, sizeof(ia3)); memset(ia4, 0, sizeof(ia4));
  memset(lAp1, 0, sizeof(lAp1)); memset(lD1, 0, sizeof(lD1)); memset(lAp2, 0, sizeof(lAp2)); memset(lD2, 0, sizeof(lD2));
  memset(rAp1, 0, sizeof(rAp1)); memset(rD1, 0, sizeof(rD1)); memset(rAp2, 0, sizeof(rAp2)); memset(rD2, 0, sizeof(rD2));
  if (ecoL) { memset(ecoL, 0, ECO_N * 2); memset(ecoR, 0, ECO_N * 2); }
  lpL = lpR = bwIn = fbL = fbR = xAcc = pLo = pRo = pLn = pRn = 0; pFase = 0; ecoLpL = ecoLpR = eHpL = eHpR = 0; ecoW = 0;
  memset(ks, 0, sizeof(ks)); memset(pb, 0, sizeof(pb)); memset(bv, 0, sizeof(bv)); memset(pad, 0, sizeof(pad)); memset(fm, 0, sizeof(fm)); memset(gr2, 0, sizeof(gr2));
  if (gBuf2) memset(gBuf2, 0, G2_N * 2);
  gLpL = gLpR = 0; padSet = 0;
  padFL = padFR = SvfS{0, 0};
  cmpEnv = 0; cmpGan = 1; cmpGT = 1; lpOL = lpOR = 0; hpL = hpR = 0; duck = 0; padGate = 1;
}
IRAM_ATTR void sanar() { resetAudio(); g_heal = g_heal + 1; }       // si la mezcla deja de ser finita se limpia todo y sigue sonando

// ─── disparadores ───
IRAM_ATTR void trigBajo(int midi, bool acento) {
  int n = bCur ^ 1; VBajo &o = bv[bCur], &q = bv[n];
  o.ampT = 0.0f;                                                      // la anterior se apaga con su cola (≈35 ms)
  float hz = noteHz(midi);
  q.incT = hz * INC_K; q.mip = mipDe(hz);
  q.inc = (glideOn && o.amp > 0.02f) ? o.inc : q.incT;                // glide: la nueva arranca donde iba la anterior y desliza
  q.ampT = acento ? 1.0f : 0.8f; q.fenv = acento ? (bTim == 1 ? 1.9f : 1.3f) : 1.0f;
  bCur = n;
  if (bassMidi >= 0) midi3(0x85, bassMidi, 0);
  midi3(0x95, midi, acento ? 110 : 90); bassMidi = midi;
}
IRAM_ATTR void trigKS(int midi, float vel) {
  int k = 0; float mn = 1e9f;
  for (int i = 0; i < NKS; i++) { if (!ks[i].on) { k = i; mn = -1; break; } if (ks[i].env < mn) { mn = ks[i].env; k = i; } }
  VKS &q = ks[k];
  float hz = noteHz(midi);
  static const float T60[4] = { 1.4f, 4.5f, 0.55f, 7.0f };            // ARPA · CUERDA · MARIMBA · VIDRIO
  static const float BRI[4] = { 0.55f, 0.50f, 0.35f, 0.85f };
  float bright = cl01(melBr);
  float c = (0.25f + 0.70f * bright) * (0.6f + 0.8f * BRI[mSon]); if (c > 0.97f) c = 0.97f;
  float D = SR / hz - (1.0f - c) / c; if (D < 8.0f) D = 8.0f; if (D > (float)(KS_N - 4)) D = (float)(KS_N - 4);
  q.D = D; q.c = c; q.g = expf(-D * 6.9f / (SR * T60[mSon])); if (q.g > 0.9995f) q.g = 0.9995f;
  int N = (int)D + 3; if (N > KS_N - 1) N = KS_N - 1;
  float sm = 0, mean = 0; float kk = 0.25f + 0.65f * bright;          // excitación: ruido suavizado (más brillante = menos suavizado)
  for (int i = 0; i < N; i++) { sm += kk * (ruido() - sm); q.buf[i] = sm; mean += sm; }
  mean /= (float)N; float pk = 1e-6f; for (int i = 0; i < N; i++) { q.buf[i] -= mean; if (fabsf(q.buf[i]) > pk) pk = fabsf(q.buf[i]); }
  float a = vel / pk; for (int i = 0; i < N; i++) q.buf[i] *= a;
  q.w = N; q.lp = 0; q.env = 0.5f; q.on = true;
  float pan = rrange(-0.6f, 0.6f); q.gL = cosf((pan + 1.0f) * 0.7854f); q.gR = sinf((pan + 1.0f) * 0.7854f);
}

IRAM_ATTR void trigFM(int midi, float vel) {
  int k = 0; float mn = 1e9f;
  for (int i = 0; i < NFM; i++) { if (!fm[i].on) { k = i; mn = -1; break; } if (fm[i].amp < mn) { mn = fm[i].amp; k = i; } }
  VFM &q = fm[k]; float hz = noteHz(midi);
  q.ic = hz * INC_K; q.im = hz * 3.5f * INC_K; q.pc = q.pm = 0;
  float br = cl01(melBr);
  q.idx = 1.2f + 3.0f * br; q.xd = expf(-1.0f / (0.9f * SR)); q.amp = vel * 0.8f; q.ad = expf(-1.0f / (1.6f * SR));
  float pan = rrange(-0.6f, 0.6f); q.gL = cosf((pan + 1.0f) * 0.7854f); q.gR = sinf((pan + 1.0f) * 0.7854f); q.on = true;
}
// ─── armonía y acordes ───
IRAM_ATTR void acordeCambio() {                                       // nuevo acorde: pad y resonador cruzan de un juego al otro
  static const int8_t V[5][4] = { { 0, 2, 4, -9 }, { 0, 4, 9, -9 }, { 0, 1, 4, -9 }, { 0, 3, 4, -9 }, { 0, 2, 4, 6 } };
  padN = (voic == 4) ? 4 : 3;
  int s = padSet ^ 1;
  for (int j = 0; j < 4; j++) {
    SlotP &p = pad[s][j];
    if (j < padN) {
      int m = midiEsc(grado + V[voic][j], tonBase() - 12);
      float hz = noteHz(m);
      p.hz = hz; p.inc[0] = (uint32_t)(hz * INC_K); p.inc[1] = (uint32_t)(hz * 1.0045f * INC_K); p.mip = mipDe(hz);
      p.dr = 0; p.drT = rrange(-1.0f, 1.0f); p.tim = 0.5f; p.timT = rnd01();
      if (padMidi[j] >= 0) midi3(0x81 + j, padMidi[j], 0);                  // MPE: cada nota del acorde en su canal (2…5)
      midi3(0xE1 + j, 0x00, 0x40); midi3(0x91 + j, m, 80); padMidi[j] = m;
      float pan = (j & 1) ? 0.62f : -0.62f; p.pl = cosf((pan + 1.0f) * 0.7854f); p.pr = sinf((pan + 1.0f) * 0.7854f);
      p.gT = 1.0f;
    } else { p.gT = 0.0f; if (padMidi[j] >= 0) { midi3(0x81 + j, padMidi[j], 0); padMidi[j] = -1; } }
    pad[padSet][j].gT = 0.0f;
  }
  padSet = s;
  int rs = rbSet ^ 1; int base = tonBase() - 12 + 12 * (alt - 1);
  static const int8_t Q[4] = { 0, 7, 12, 19 }, O[4] = { 0, 12, 24, 36 };
  for (int j = 0; j < NCB; j++) {
    int m;
    if (modo == 0) { static const int8_t A[4] = { 0, 2, 4, 7 }; m = midiEsc(grado + A[j], base); }
    else if (modo == 1) m = midiEsc(grado, base) + Q[j];
    else if (modo == 2) m = midiEsc(grado, base) + O[j];
    else { static const int8_t B[4] = { 0, 1, 2, 4 }; m = midiEsc(grado + B[j], base); }
    while (m > 84) m -= 12;
    float D = SR / noteHz(m) - (1.0f - rbDamp) / rbDamp; if (D < 8.0f) D = 8.0f; if (D > (float)(KS_N - 4)) D = (float)(KS_N - 4);
    pb[rs][j].D = D;
  }
  rbSet = rs;
  g_acorde = grado;
}
IRAM_ATTR int melNota(bool fuerte) {
  static const int8_t D[8] = { -2, -1, -1, 1, 1, 1, 2, 3 };
  int n = ESC_N[esc];
  int d = D[xr() & 7]; mIdx += d;
  int lo = -(n / 2), hi = n + n / 2 - 1;
  if (mIdx > hi) mIdx = hi - 1; if (mIdx < lo) mIdx = lo + 1;
  if (fuerte && !esTonoAcorde(mIdx) && (xr() & 3)) { for (int k = 1; k <= 2; k++) { if (esTonoAcorde(mIdx - k)) { mIdx -= k; break; } if (esTonoAcorde(mIdx + k)) { mIdx += k; break; } } }
  return midiEsc(mIdx, tonBase() + 12 * (mOct - 1));
}
IRAM_ATTR void melGenerar() { for (int i = 0; i < 16; i++) melHit[i] = ((i & 3) == 0) ? (rnd01() < 0.75f) : (rnd01() < 0.40f); melHit[0] = true; }
IRAM_ATTR void aplicarEscena(int n) {                                 // una escena = escala + progresión + patrón de bajo + voicing + sonido + modo del resonador
  const Escena &e = ESCENAS[n];
  reqEsc = e.esc; reqProg = e.prog; reqBPat = e.bpat; reqVoic = e.voic; reqMSon = e.msn; reqModo = e.modo; reqPadRit = e.prit; reqBTim = e.btim;
  escena = n; g_escena = n;
}
// se llama al comenzar cada compás: aquí (y sólo aquí) entran las peticiones del control
IRAM_ATTR void compas() {
  barN++;
  if (reqAuto && (barN % 16) == 0) { int n = (int)(xr() % 7); if (n >= escena) n++; reqEscena = n; }   // "follow action": cada 16 compases pasa a otra escena
  if (reqEscena != escena) aplicarEscena(reqEscena);
  esc = reqEsc % NESC; ton = reqTon % 12; bPat = reqBPat & 7; bOct = reqBOct % 3; glideOn = reqGlide & 1; prog = reqProg % 6; voic = reqVoic % 5;
  bTim = reqBTim % 3; glideOn = (bTim == 1); poli = reqPoli & 7; lenB = POLI[poli][0]; lenM = POLI[poli][1];
  padRit = reqPadRit % 3; mSon = reqMSon % 5; mOct = reqMOct % 3; modo = reqModo & 3; alt = reqAlt % 3; frz = reqFrz & 1;
  static uint32_t seenGen = 0; if (reqMGen != seenGen) { seenGen = reqMGen; melGenerar(); }
  g_esc = esc; g_ton = ton;
  float pz = par[1][1]; int cb = pz < 0.34f ? 1 : (pz < 0.67f ? 2 : 4);        // ACORDES POT2: acorde nuevo cada 1, 2 o 4 compases
  static int cbPrev = -1, voicPrev = -1, modoPrev = -1, altPrev = -1, escPrev = -1, tonPrev = -1, progPrev = -1;
  bool adv = ((barN % (uint32_t)cb) == 0);
  if (adv) chordIdx = (chordIdx + 1) & 3;
  bool cfg = (cb != cbPrev || voic != voicPrev || modo != modoPrev || alt != altPrev || esc != escPrev || ton != tonPrev || prog != progPrev);
  cbPrev = cb; voicPrev = voic; modoPrev = modo; altPrev = alt; escPrev = esc; tonPrev = ton; progPrev = prog;
  grado = PROGS[prog][chordIdx] % ESC_N[esc];
  if (adv || cfg) acordeCambio();
}
inline int divDe(float p) { return p < 0.34f ? 4 : (p < 0.67f ? 2 : 1); }       // POT2 de BAJO y MELODIA: rejilla 1/4 · 1/8 · 1/16
IRAM_ATTR void paso() {
  int s = stepPos; stepPos = (stepPos + 1) & 15; g_paso = s;
  if (s == 0) compas();
  int sB = cB; cB = (cB + 1) % lenB; int sM = cM; cM = (cM + 1) % lenM;               // cada pista cuenta su propio largo de patrón sobre el mismo reloj
  bool corre0 = runMask & 1, corre1 = runMask & 2, corre2 = runMask & 4;
  // BAJO
  if (bGateSteps > 0 && --bGateSteps == 0) { bv[bCur].ampT = 0.0f; if (bassMidi >= 0) { midi3(0x85, bassMidi, 0); bassMidi = -1; } }
  int dz = divDe(par[0][1]);
  char ch = BPAT[bPat][sB & 15];
  if (corre0 && ch != '-' && (sB % dz) == 0) {
    int d = 1; while (d < 16 && BPAT[bPat][(sB + d) & 15] == '-') d++;
    trigBajo(midiEsc(grado, tonBase() - 24 + 12 * (bOct - 1)) + (ch == '2' ? 12 : 0), ch == '2' || s == 0);
    bGateSteps = d > 1 ? d - 1 : 1; g_pul[0] = 1.0f;
  }
  // ACORDES pulsados (puerta en corcheas con bordes de 5 ms)
  padGateT = (padRit == 1) ? ((s & 1) ? 0.0f : 1.0f) : (padRit == 2 ? (STABP[s] == '1' ? 1.0f : 0.0f) : 1.0f);
  if (corre1 && (s & 3) == 0) g_pul[1] = 1.0f;
  // MELODIA
  int dm = divDe(par[2][1]);
  if (corre2 && melHit[sM & 15] && (sM % dm) == 0 && rnd01() < 0.15f + 0.85f * par[2][3]) {
    int nota = melNota((sM & 3) == 0);
    int sh = (int)lroundf(modPitch * 7.0f); if (sh != 0) nota = cuantEsc(nota + sh);              // la matriz puede mover el tono, siempre cuantizado a la escala
    if (mSon == 4) trigFM(nota, 0.5f + 0.5f * rnd01()); else trigKS(nota, 0.5f + 0.5f * rnd01());
    if (melMidi >= 0) midi3(0x86, melMidi, 0);
    midi3(0x96, nota, 96); melMidi = nota; g_pul[2] = 1.0f;
  }
  if (s == 0) g_pul[4] = 1.0f;
}

// ==============================================================================================================================================
// RENDER
// ==============================================================================================================================================
const float K_SLEW = 0.0006f;
IRAM_ATTR void renderCore(int32_t *buffer) {
  const uint8_t run = runMask;
  // pendientes (para la pantalla): lo pedido y todavía no aplicado
  g_pend = (uint8_t)(((reqEsc % NESC) != esc) + ((reqTon % 12) != ton) + (reqEscena != escena) + ((reqBPat & 7) != bPat) + ((reqBOct % 3) != bOct) + ((reqProg % 6) != prog) + ((reqVoic % 5) != voic) + ((reqMSon % 5) != mSon) + ((reqModo & 3) != modo) + ((reqAlt % 3) != alt) + ((reqPadRit % 3) != padRit) + ((reqBTim % 3) != bTim) + ((reqMOct % 3) != mOct) + ((reqGlide & 1) != glideOn));
  // reloj maestro: tempo global (POT2 de SESION) + swing
  const float bpm = 70.0f + 90.0f * par[4][1]; g_bpm = bpm;
  stepBase = SR * 60.0f / (bpm * 4.0f);
  const float sw = par[4][2] * 0.33f;
  // MATRIZ DE MODULACIÓN: fuentes LFO1, LFO2 (a razón 3:2), MSEG y ENV (seguidor del bajo) → 7 destinos, con cantidad por ruta y profundidad global
  float modD[7] = { 0, 0, 0, 0, 0, 0, 0 };
  {
    const bool mOn = (run & 32) != 0; const float depth = mOn ? par[6][0] : 0.0f;
    static const float BPC[6] = { 16, 8, 4, 2, 1, 0.5f };
    int ri = (int)(par[6][1] * 5.999f); float adv = (bpm / 60.0f) * ((float)BUFFER_SAMPLES / SR) / BPC[ri];
    for (int k = 0; k < 2; k++) {
      lfoPh[k] += adv * (k ? 1.5f : 1.0f);
      if (lfoPh[k] >= 1.0f) { lfoPh[k] -= 1.0f; lfoSH[k] = rrange(-1.0f, 1.0f); g_pul[6] = 1.0f; }
      float ph = lfoPh[k], v;
      switch (lfoShape[k] & 3) { case 0: v = sinPh((uint32_t)(ph * 4294967040.0f)); break; case 1: v = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break; case 2: v = lfoSH[k]; break; default: v = ph * 2.0f - 1.0f; }
      srcV[k] = v;
    }
    int L = par[6][3] < 0.34f ? 1 : (par[6][3] < 0.67f ? 2 : 4);
    int cur = (stepPos + 15) & 15;
    float fr = 1.0f - (float)stepCnt / stepBase; if (fr < 0) fr = 0; if (fr > 1) fr = 1;
    float ph = ((float)((barN % (uint32_t)L) * 16 + cur) + fr) / (16.0f * (float)L); if (ph > 1.0f) ph = 1.0f;
    const Punto *P = MSEG[msegSel & 7]; float v = P[5].v;
    for (int q = 0; q < 5; q++) if (ph <= P[q + 1].t) { float t0 = P[q].t, t1 = P[q + 1].t, f = (t1 > t0) ? (ph - t0) / (t1 - t0) : 1.0f; v = P[q].v + (P[q + 1].v - P[q].v) * f; break; }
    srcV[2] = v * 2.0f - 1.0f; srcV[3] = duck * 2.0f - 1.0f;
    for (int d = 0; d < 7; d++) { float a = 0; for (int sx = 0; sx < 4; sx++) a += mAmt[sx][d] * srcV[sx]; a *= depth; modD[d] = a > 1.0f ? 1.0f : (a < -1.0f ? -1.0f : a); }
  }
  melBr = cl01(par[2][2] + modD[3] * 0.4f); modPitch = modD[6];
  // suavizado por buffer (volúmenes de capa y maestro)
  for (int l = 0; l < 4; l++) { float v = par[l][0]; vS[l] += (v * v - vS[l]) * 0.08f; runG[l] += ((((run >> l) & 1) ? 1.0f : 0.0f) - runG[l]) * 0.10f; }
  { float v = par[5][0]; vS[5] += (v * v - vS[5]) * 0.08f; runG[4] += ((((run >> 4) & 1) ? 1.0f : 0.0f) - runG[4]) * 0.10f; }
  { float v = par[4][0]; vMs += (1.3f * v * v - vMs) * 0.08f; }
  const float sendEnv = cl01(par[4][3] + modD[5] * 0.35f);
  revS += (sendEnv - revS) * 0.05f; ecoS += (sendEnv - ecoS) * 0.05f;
  // filtros por buffer
  { float fb = (70.0f + 1500.0f * par[0][2] * par[0][2]) * exp2f(modD[0] * 1.5f);
    for (int v = 0; v < 2; v++) { float fc = fb * (1.0f + (bTim == 1 ? 4.5f : 2.5f) * bv[v].fenv); if (fc > (bTim == 1 ? 5200.0f : 3800.0f)) fc = (bTim == 1 ? 5200.0f : 3800.0f);
      Coef c = coefDe(gDe(fc), bTim == 1 ? 0.20f : 0.55f - 0.30f * par[0][2]); rampaHacia(bv[v].r, c, 1.0f / BUFFER_SAMPLES); bv[v].fenv *= (bTim == 1 ? 0.90f : 0.93f); }
    float fp = (220.0f + 4800.0f * par[1][2] * par[1][2]) * exp2f(modD[1] * 1.5f); if (fp > 9000.0f) fp = 9000.0f;
    Coef cp = coefDe(gDe(fp), 0.7f); rampaHacia(padR, cp, 1.0f / BUFFER_SAMPLES); }
  const float sub = 0.35f + 0.65f * (1.0f - par[0][3]), saw = 0.10f + 0.55f * par[0][3];
  { float at = 0.015f + 0.8f * par[1][3] * par[1][3]; padAk = 1.0f - expf(-1.0f / (at * SR * 0.33f)); }
  rbFb = 0.80f + 0.17f * par[3][1] + modD[4] * 0.06f; if (rbFb > 0.97f) rbFb = 0.97f; if (rbFb < 0.5f) rbFb = 0.5f; if (frz) rbFb = 0.985f;
  rbDamp = 0.15f + 0.75f * par[3][3];
  rbIn = par[3][2];
  const float revEnvio = (0.10f + 0.90f * revS) * 2.0f, revDecay = 0.55f + 0.35f * revS, revRet = 0.20f + 0.45f * revS;
  const float ecoFbT = 0.20f + 0.35f * ecoS;
  { int nuevo = (int)(3.0f * stepBase); if (nuevo > ECO_N - 8) nuevo = ECO_N - 8;               // eco sincronizado: corchea con puntillo
    if (ecoX <= 0.0f && abs(nuevo - ecoDA) > 24) { ecoDB = ecoDA; ecoDA = nuevo; ecoX = 1.0f; } }
  { float pos = cl01(0.28f + modD[2] * 0.6f) * 6.999f;                     // posición de la tabla de ondas (la mueve la matriz)
    if (fabsf(pos - wtPosApl) > 0.01f) { wtPosApl = pos; int i0 = (int)pos; float f = pos - (float)i0; const float *A = WT[i0], *B = WT[i0 + 1];
      for (int k = 0; k < 1025; k++) WTcur[k] = A[k] + (B[k] - A[k]) * f; } }
  for (int sdx = 0; sdx < 2; sdx++) for (int j = 0; j < 4; j++) {          // expresión POR NOTA: cada nota del acorde deriva en afinación y timbre por su cuenta
    SlotP &q = pad[sdx][j]; if (q.g < 0.0003f && q.gT == 0.0f) continue;
    if (rnd01() < 0.004f) { q.drT = rrange(-1.0f, 1.0f); q.timT = rnd01(); }
    q.dr += (q.drT - q.dr) * 0.02f; q.tim += (q.timT - q.tim) * 0.02f;
    float fct = 1.0f + q.dr * 0.0087f; q.inc[0] = (uint32_t)(q.hz * fct * INC_K); q.inc[1] = (uint32_t)(q.hz * fct * 1.0045f * INC_K);
  }
  for (int j = 0; j < 4; j++) { g_dr[j] = pad[padSet][j].dr; g_tim[j] = pad[padSet][j].tim; }
  const bool texOn = (run & 16) != 0; const float gKc = 0.03f + 0.9f * par[5][2] * par[5][2];
  float acB = 0, acP = 0, acM = 0, acR = 0, acT = 0;
  for (int i = 0; i < BUFFER_SAMPLES; i++) {
    rampaAvanza(bv[0].r); rampaAvanza(bv[1].r); rampaAvanza(padR);
    if (--stepCnt <= 0) { stepCnt += (int)(((stepPos & 1) ? (1.0f - sw) : (1.0f + sw)) * stepBase); paso(); }
    // — BAJO —
    float bo = 0;
    for (int v = 0; v < 2; v++) {
      VBajo &q = bv[v]; if (q.amp < 0.0004f && q.ampT == 0.0f) continue;
      q.inc += (q.incT - q.inc) * 0.004f;                              // glide / afinación suave
      uint32_t inc = (uint32_t)q.inc; q.ph += inc; q.phS += inc >> 1; q.ph2 += inc + (inc >> 7);
      float o;
      if (bTim == 1) o = leeTabla(tabSaw[q.mip], q.ph) * 0.9f + sinPh(q.phS) * 0.18f;                                  // ACIDO: sierra + resonancia alta + saturación
      else if (bTim == 2) o = (leeTabla(tabSaw[q.mip], q.ph) + leeTabla(tabSaw[q.mip], q.ph2)) * 0.45f + sinPh(q.phS) * 0.55f;  // REESE: dos sierras desafinadas + sub
      else o = leeTabla(tabSaw[q.mip], q.ph) * saw + sinPh(q.phS) * sub;                                              // SUB
      o = sat3(o * (bTim == 1 ? 1.6f : 1.25f));
      o = svfLP(q.f, q.r.cur, o + 1e-18f);
      o += ((bTim == 1) ? 0.55f : 0.60f * par[0][2]) * (sat3(o * 1.8f) - o);   // más resonancia → más saturación suave de salida
      q.amp += (q.ampT - q.amp) * (q.ampT > q.amp ? 0.012f : 0.0006f);
      bo += o * q.amp;
    }
    bo *= 0.55f * runG[0] * vS[0];
    // — ACORDES —
    float pl = 0, pr = 0;
    padGate += (padGateT - padGate) * (padGateT > padGate ? 0.045f : (padRit == 2 ? 0.0007f : 0.045f));   // bordes de ≈5 ms · en STAB la cola dura ≈ 30 ms
    for (int sdx = 0; sdx < 2; sdx++) for (int j = 0; j < 4; j++) {
      SlotP &p = pad[sdx][j];
      if (p.g < 0.0003f && p.gT == 0.0f) continue;
      p.g += (p.gT - p.g) * (p.gT > p.g ? padAk : 0.0012f);          // entra con el ataque de P4; sale en ≈40 ms
      p.ph[0] += p.inc[0]; p.ph[1] += p.inc[1];
      float o = (tabPh(WTcur, p.ph[0]) + tabPh(WTcur, p.ph[1])) * p.g * (0.8f + 0.4f * p.tim);
      pl += o * p.pl; pr += o * p.pr;
    }
    pl = svfLP(padFL, padR.cur, pl * 0.20f * padGate + 1e-18f); pr = svfLP(padFR, padR.cur, pr * 0.20f * padGate + 1e-18f);
    pl += 0.30f * par[1][2] * (sat3(pl * 2.0f) - pl); pr += 0.30f * par[1][2] * (sat3(pr * 2.0f) - pr);
    float dk = 1.0f - (bTim ? 0.50f : 0.30f) * duck;                                    // sidechain suave: el bajo "respira" sobre el pad
    pl *= dk * runG[1] * vS[1]; pr *= dk * runG[1] * vS[1];
    // — MELODIA (cuerdas) —
    float ml = 0, mr = 0;
    for (int v = 0; v < NKS; v++) {
      VKS &q = ks[v]; if (!q.on) continue;
      float rp = (float)q.w - q.D; if (rp < 0) rp += (float)KS_N;
      int i0 = (int)rp; float f = rp - (float)i0; int i1 = (i0 + 1) & (KS_N - 1);
      float y = q.buf[i0] + (q.buf[i1] - q.buf[i0]) * f;
      q.lp += q.c * (y - q.lp);
      q.buf[q.w] = q.lp * q.g; q.w = (q.w + 1) & (KS_N - 1);
      q.env += (fabsf(y) - q.env) * 0.0015f;
      ml += y * q.gL; mr += y * q.gR;
      if (q.env < 0.00008f) q.on = false;
    }
    for (int v = 0; v < NFM; v++) {
      VFM &q = fm[v]; if (!q.on) continue;
      q.pm += (uint32_t)q.im; q.pc += (uint32_t)q.ic;
      float o = sinPh(q.pc + (uint32_t)(int32_t)(sinPh(q.pm) * q.idx * 6.8e8f)) * q.amp;
      q.amp *= q.ad; q.idx *= q.xd; ml += o * q.gL * 0.8f; mr += o * q.gR * 0.8f;
      if (q.amp < 0.0003f) q.on = false;
    }
    ml *= 0.55f * runG[2] * vS[2]; mr *= 0.55f * runG[2] * vS[2];
    // — RESONADOR: entra lo que suena (bajo, pad, melodía), sale el ensamble de peines afinado al acorde —
    float rin = frz ? 0.0f : (bo * 0.25f + (pl + pr) * 0.30f + (ml + mr) * 0.50f) * rbIn * (1.0f - rbFb) * 2.2f;
    float rl = 0, rr = 0;
    for (int sdx = 0; sdx < 2; sdx++) {
      float tg = (sdx == rbSet) ? 1.0f : 0.0f; rbG[sdx] += (tg - rbG[sdx]) * 0.0009f;
      if (rbG[sdx] < 0.0005f && tg == 0.0f) continue;
      for (int j = 0; j < NCB; j++) {
        Peine &c = pb[sdx][j];
        float rp = (float)c.w - c.D; if (rp < 0) rp += (float)KS_N;
        int i0 = (int)rp; float f = rp - (float)i0; int i1 = (i0 + 1) & (KS_N - 1);
        float y = c.buf[i0] + (c.buf[i1] - c.buf[i0]) * f;
        c.lp += rbDamp * (y - c.lp);
        c.buf[c.w] = rin + rbFb * c.lp; c.w = (c.w + 1) & (KS_N - 1);
        if (j & 1) rr += y * rbG[sdx]; else rl += y * rbG[sdx];
      }
    }
    float rlv = runG[3] * vS[3] * 0.9f; rl *= rlv; rr *= rlv;
    // — TEXTURA: granos de 10-100 ms del propio remuestreo, afinados a la escala y al acorde —
    float gl2 = 0, gr2v = 0;
    if (texOn || runG[4] > 0.001f) {
      if (!reqGFrz) { gBuf2[gW2] = (int16_t)((bo + (pl + pr) * 0.5f + (ml + mr) * 0.5f) * 12000.0f); gW2 = (gW2 + 1) & (G2_N - 1); }
      if (--gCnt2 <= 0 && texOn) {
        gCnt2 = (int)(SR * (0.012f + 0.30f * (1.0f - par[5][1])) * rrange(0.7f, 1.3f));
        for (int k = 0; k < 6; k++) if (!gr2[k].on) {
          static const float SZ[4] = { 0.010f, 0.025f, 0.050f, 0.100f };
          float spread = par[5][3]; int nq = 1 + (int)(spread * 3.999f); int arm = reqGArm & 3; int semi = 0;
          static const int8_t Q[4] = { 0, 2, 4, 7 }, O[4] = { 0, -12, 12, 24 }, Qt[4] = { 0, 7, 12, 19 };
          int q = xr() % (uint32_t)nq;
          if (arm == 1) semi = midiEsc(grado + Q[q], 60) - midiEsc(grado, 60); else if (arm == 2) semi = O[q]; else if (arm == 3) semi = Qt[q];
          Grano2 &g = gr2[k]; g.len = (int)(SZ[reqGSize & 3] * SR); g.age = 0; g.rate = powf(2.0f, (float)semi * (1.0f / 12.0f));
          g.pos = (float)((gW2 - (int)(2.2f * (float)g.len) - 1500 - (int)(spread * 15000.0f * rnd01())) & (G2_N - 1));
          g.amp = rrange(0.5f, 1.0f); float pn = rrange(0.1f + 0.4f * (1.0f - spread), 0.9f - 0.4f * (1.0f - spread)); g.gL = 1.414f * sqrtf(1.0f - pn); g.gR = 1.414f * sqrtf(pn); g.on = true; g_pul[5] = 1.0f; break;
        }
      }
      for (int k = 0; k < 6; k++) {
        Grano2 &g = gr2[k]; if (!g.on) continue;
        int ip = (int)g.pos; float fr = g.pos - (float)ip;
        float a = (float)gBuf2[ip & (G2_N - 1)], b2 = (float)gBuf2[(ip + 1) & (G2_N - 1)];
        float w = sinPh((uint32_t)((float)g.age / (float)g.len * 2147483000.0f));
        float v = (a + (b2 - a) * fr) * (1.0f / 12000.0f) * w * g.amp;
        gl2 += v * g.gL; gr2v += v * g.gR;
        g.pos += g.rate; if (g.pos >= (float)G2_N) g.pos -= (float)G2_N;
        if (++g.age >= g.len) g.on = false;
      }
      gLpL += gKc * (gl2 - gLpL); gLpR += gKc * (gr2v - gLpR);
      float tv = runG[4] * vS[5] * 0.9f; gl2 = gLpL * tv; gr2v = gLpR * tv;
      acT += fabsf(gl2) + fabsf(gr2v);
    }
    // — bus —
    float bAbs = fabsf(bo); duck += (bAbs * 2.0f - duck) * (bAbs * 2.0f > duck ? 0.01f : 0.0003f); if (duck > 1.0f) duck = 1.0f;
    float dryL = bo + pl + ml + rl + gl2, dryR = bo + pr + mr + rr + gr2v;
    acB += bAbs; acP += fabsf(pl) + fabsf(pr); acM += fabsf(ml) + fabsf(mr); acR += fabsf(rl) + fabsf(rr);
    // — eco ping-pong —
    int rA = ecoW - ecoDA; if (rA < 0) rA += ECO_N;
    float oL = ecoL[rA] * (1.0f / 32768.0f), oR = ecoR[rA] * (1.0f / 32768.0f);
    if (ecoX > 0.0f) {
      int rB = ecoW - ecoDB; if (rB < 0) rB += ECO_N;
      oL += (ecoL[rB] * (1.0f / 32768.0f) - oL) * ecoX; oR += (ecoR[rB] * (1.0f / 32768.0f) - oR) * ecoX;
      ecoX -= ECO_XF; if (ecoX < 0.0f) ecoX = 0.0f;
    }
    ecoLpL += 0.30f * (oL - ecoLpL); ecoLpR += 0.30f * (oR - ecoLpR);
    eHpL += ECO_HP * (ecoLpL - eHpL); float lzL = ecoLpL - eHpL;
    eHpR += ECO_HP * (ecoLpR - eHpR); float lzR = ecoLpR - eHpR;
    ecoFb += (ecoFbT - ecoFb) * 0.0006f;
    float sEco = ((pl + pr) * 0.18f + (ml + mr) * 0.55f + (rl + rr) * 0.12f + (gl2 + gr2v) * 0.20f) * (0.05f + 1.0f * ecoS);
    float wL = sEco + lzR * ecoFb, wR = sEco * 0.9f + lzL * ecoFb;
    if (wL > 0.8f || wL < -0.8f) wL = limitador(wL);
    if (wR > 0.8f || wR < -0.8f) wR = limitador(wR);
    ecoL[ecoW] = (int16_t)(wL * 30000.0f); ecoR[ecoW] = (int16_t)(wR * 30000.0f);
    if (++ecoW >= ECO_N) ecoW = 0;
    float retEL = lzL * 0.8f, retER = lzR * 0.8f;
    // — placa (Dattorro) a media tasa —
    xAcc += (pl + pr) * 0.28f + (ml + mr) * 0.45f + (rl + rr) * 0.22f + (bo) * 0.03f + (gl2 + gr2v) * 0.30f;
    if (pFase == 0) { pFase = 1; }
    else {
      pFase = 0;
      float x = xAcc * 0.5f * revEnvio; xAcc = 0;
      bwIn += 0.80f * (x + 1e-18f - bwIn);
      float decay = revDecay;
      float d = apFijo(ia1, 142, ii1, bwIn, 0.75f);
      d = apFijo(ia2, 107, ii2, d, 0.75f);
      d = apFijo(ia3, 379, ii3, d, 0.625f);
      d = apFijo(ia4, 277, ii4, d, 0.625f);
      lfo1 += lfo1Inc * 2u; lfo2 += lfo2Inc * 2u;
      float mod1 = sinPh(lfo1) * 14.0f, mod2 = sinPh(lfo2) * 14.0f;
      float xl = d + decay * fbR;
      float a = apMod(lAp1, 672 + 40, lAp1w, xl, 0.70f, 672.0f + 20.0f + mod1);
      float d1 = lD1[lD1w]; lD1[lD1w] = a; if (++lD1w >= 4453) lD1w = 0;
      lpL += 0.68f * (d1 - lpL);
      float t2 = apFijo(lAp2, 1800, lAp2w, lpL * decay, 0.50f);
      float d2 = lD2[lD2w]; lD2[lD2w] = t2; if (++lD2w >= 3720) lD2w = 0;
      fbL = d2;
      float xr2 = d + decay * fbL;
      float b = apMod(rAp1, 908 + 40, rAp1w, xr2, 0.70f, 908.0f + 20.0f + mod2);
      float e1 = rD1[rD1w]; rD1[rD1w] = b; if (++rD1w >= 4217) rD1w = 0;
      lpR += 0.68f * (e1 - lpR);
      float u = apFijo(rAp2, 2656, rAp2w, lpR * decay, 0.50f);
      float e2 = rD2[rD2w]; rD2[rD2w] = u; if (++rD2w >= 3163) rD2w = 0;
      fbR = e2;
      pLo = pLn; pRo = pRn;
      pLn = (tomar(rD1, 4217, rD1w, 266) + tomar(rD1, 4217, rD1w, 2974) - tomar(rAp2, 2656, rAp2w, 1913) - tomar(lD1, 4453, lD1w, 1990)) * 0.70f;
      pRn = (tomar(lD1, 4453, lD1w, 353) + tomar(lD1, 4453, lD1w, 3627) - tomar(lAp2, 1800, lAp2w, 1228) - tomar(rD1, 4217, rD1w, 2111)) * 0.70f;
    }
    float rvL = 0.5f * (pLo + pLn) * revRet, rvR = 0.5f * (pRo + pRn) * revRet;
    // — maestro: pasa-altos (continua) → auto-nivel → limitador → techo de agudos —
    float mL = dryL + retEL + rvL, mR = dryR + retER + rvR;
    hpL += 0.0035f * (mL - hpL); mL -= hpL; hpR += 0.0035f * (mR - hpR); mR -= hpR;
    if (!(mL > -16.0f && mL < 16.0f && mR > -16.0f && mR < 16.0f)) { sanar(); mL = mR = 0.0f; }
    const float gain = 0.80f * vMs;
#if DIAG
    { float a = fabsf(mL * gain); if (a > dgPk) dgPk = a; if (a > 0.75f) dgSobre++; }
#endif
    {
      float pk = fabsf(mL * gain), pr2 = fabsf(mR * gain); if (pr2 > pk) pk = pr2;
      cmpEnv += (pk - cmpEnv) * (pk > cmpEnv ? 0.02f : 0.00008f);
      if ((i & 7) == 0) cmpGT = (cmpEnv > 0.5f) ? 0.5f / cmpEnv : 1.0f;
      cmpGan += (cmpGT - cmpGan) * (cmpGT < cmpGan ? 0.02f : 0.0004f);
    }
    float oL2 = limitador(mL * gain * cmpGan), oR2 = limitador(mR * gain * cmpGan);
    lpOL += 0.78f * (oL2 - lpOL); lpOR += 0.78f * (oR2 - lpOR); oL2 = lpOL; oR2 = lpOR;
    buffer[i * 2]     = (int32_t)lrintf(oL2 * 8200000.0f) << 8;
    buffer[i * 2 + 1] = (int32_t)lrintf(oR2 * 8200000.0f) << 8;
  }
  const float inv = 1.0f / BUFFER_SAMPLES;
  g_niv[0] = cl01(acB * inv * 4.0f); g_niv[1] = cl01(acP * inv * 6.0f); g_niv[2] = cl01(acM * inv * 5.0f); g_niv[3] = cl01(acR * inv * 6.0f); g_niv[5] = cl01(acT * inv * 6.0f); g_niv[6] = cl01(fabsf(srcV[0]) * 0.5f + fabsf(srcV[2]) * 0.5f);
  for (int l = 0; l < NCAPAS; l++) g_pul[l] *= 0.93f;
}
IRAM_ATTR void renderBuffer() {
  int32_t buffer[BUFFER_SAMPLES * 2];
  uint32_t t0 = micros();
  renderCore(buffer);
  uint32_t dt = micros() - t0;
  g_usAvg = (g_usAvg * 15 + dt) / 16;
  if (dt > g_usMax) g_usMax = dt;
  if (dt > BUF_US - 150) { xrunAt = millis(); g_xruns = g_xruns + 1; }
  size_t written;
  i2s_channel_write(tx_chan, buffer, sizeof(buffer), &written, portMAX_DELAY);
}
void i2s_init() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true; chan_cfg.dma_desc_num = 6; chan_cfg.dma_frame_num = 240;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)I2S_BCK, .ws = (gpio_num_t)I2S_LCK, .dout = (gpio_num_t)I2S_DIN, .din = I2S_GPIO_UNUSED, .invert_flags = { false, false, false } },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}


// ==============================================================================================================================================
// VALORES POR CAPA (escalado de valor: las perillas no saltan), ESCENA EN LA SD
// ==============================================================================================================================================
float guardado[NCAPAS][4] = { { 0.80f, 0.50f, 0.45f, 0.50f }, { 0.70f, 0.20f, 0.55f, 0.35f }, { 0.70f, 0.50f, 0.50f, 0.50f }, { 0.60f, 0.50f, 0.50f, 0.50f }, { 0.80f, 0.62f, 0.15f, 0.40f }, { 0.60f, 0.55f, 0.65f, 0.35f }, { 0.60f, 0.50f, 0.50f, 0.34f } };
bool  primeraLectura[4] = { true, true, true, true }, bloqueado[4] = { true, true, true, true }, entrada[4] = { false, false, false, false };
float p0[4] = { 0, 0, 0, 0 }, potUlt[4] = { -1, -1, -1, -1 };
volatile int capaCtl = 0; volatile bool menuAbierto = false; int menuSel = 0;
float flashLevel = 0.0f; float meterV = 0.0f; unsigned long meterT = 0; uint8_t meterHue = 0, capaHue = 0;
unsigned long capaT = 0; char toastBuf[26] = ""; unsigned long toastT = 0; bool escenaCargada = false;
void toast(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(toastBuf, sizeof(toastBuf), fmt, ap); va_end(ap); toastT = millis(); }
static const char *CAPA_NOM[NCAPAS] = { "BAJO", "ACORDES", "MELODIA", "RESONADOR", "SESION", "TEXTURA", "MATRIZ" };
static const uint8_t HUE_CAPA[NCAPAS] = { 0, 96, 190, 130, 50, 215, 30 };
#define NORD 7
static const uint8_t ORDEN[NORD] = { 0, 1, 2, 5, 3, 6, 4 };       // BAJO · ACORDES · MELODIA · TEXTURA · RESONADOR · MATRIZ · SESION
static int posDe(int capa) { for (int i = 0; i < NORD; i++) if (ORDEN[i] == capa) return i; return 0; }
void cambiarCapa(int n) { for (int q = 0; q < 4; q++) { bloqueado[q] = false; entrada[q] = true; } capaCtl = n; flashLevel = 0.6f; capaT = millis(); toastT = 0; }
void leerPotCapa(int p, float v) {
  int capa = capaCtl; float out = v;
  if (primeraLectura[p]) { primeraLectura[p] = false; bloqueado[p] = true; guardado[capa][p] = v; par[capa][p] = v; }
  else {
    if (entrada[p]) { entrada[p] = false; p0[p] = v; }
    if (!bloqueado[p]) {
      float v0 = guardado[capa][p], pp = p0[p];
      if (v >= pp) out = v0 + (v - pp) / (1.0f - pp > 0.02f ? 1.0f - pp : 0.02f) * (1.0f - v0);
      else         out = v0 * v / (pp > 0.02f ? pp : 0.02f);
      if (out < 0) out = 0; if (out > 1) out = 1;
      if (fabsf(v - out) < 0.03f) bloqueado[p] = true;
    }
    if (fabsf(out - guardado[capa][p]) > 0.0015f) { guardado[capa][p] = out; par[capa][p] = out; meterV = out; meterT = millis(); meterHue = HUE_CAPA[capa]; if (capa == 6 && p == 2) mAmt[mSrcSel & 3][mDstSel % 7] = out * 2.0f - 1.0f; }
  }
  bool alguno = false; for (int q = 0; q < 4; q++) if (!bloqueado[q]) alguno = true;
  g_sinRecoger = alguno;
}
#define SD_DIR "/SESION"
#define SD_ARCH "/SESION/escena.txt"
#define SD_TMP "/SESION/escena.tmp"
static char escenaBuf[1100], escenaUlt[1100], escenaGuard[1100];
int armarEscena(char *b, int n) {
  int k = snprintf(b, n, "v=1\ncapa=%d\n", (int)capaCtl);
  for (int c = 0; c < NCAPAS; c++) k += snprintf(b + k, n - k, "p%d=%.3f,%.3f,%.3f,%.3f\n", c, guardado[c][0], guardado[c][1], guardado[c][2], guardado[c][3]);
  k += snprintf(b + k, n - k, "esc=%u\nton=%u\nscn=%u\nbpat=%u\nboct=%u\ngli=%u\nprog=%u\nvoic=%u\nprit=%u\nmson=%u\nmoct=%u\nmodo=%u\nalt=%u\nfrz=%u\nauto=%u\nrun=%u\nbtim=%u\npoli=%u\ngsz=%u\ngarm=%u\ngfrz=%u\nsrc=%u\ndst=%u\nlfa=%u\nlfb=%u\nmseg=%u\n",
                (unsigned)reqEsc, (unsigned)reqTon, (unsigned)reqEscena, (unsigned)reqBPat, (unsigned)reqBOct, (unsigned)reqGlide, (unsigned)reqProg, (unsigned)reqVoic, (unsigned)reqPadRit,
                (unsigned)reqMSon, (unsigned)reqMOct, (unsigned)reqModo, (unsigned)reqAlt, (unsigned)reqFrz, (unsigned)reqAuto, (unsigned)(runMask & 0x3F), (unsigned)reqBTim, (unsigned)reqPoli, (unsigned)reqGSize, (unsigned)reqGArm, (unsigned)reqGFrz, (unsigned)mSrcSel, (unsigned)mDstSel, (unsigned)lfoShape[0], (unsigned)lfoShape[1], (unsigned)msegSel);
  k += snprintf(b + k, n - k, "mat="); for (int a = 0; a < 4; a++) for (int d = 0; d < 7; d++) k += snprintf(b + k, n - k, "%.2f%c", mAmt[a][d], (a == 3 && d == 6) ? '\n' : ',');
  return k;
}
bool aplicarTexto(const char *txt) {
  int capa = 0, nCapas = 0, i; unsigned u; float v[4];
  for (const char *l = txt; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
    if (sscanf(l, "capa=%d", &i) == 1) capa = (i < 0 || i >= NCAPAS) ? 0 : i;
    else if (l[0] == 'p' && sscanf(l, "p%d=%f,%f,%f,%f", &i, &v[0], &v[1], &v[2], &v[3]) == 5 && i >= 0 && i < NCAPAS) {
      for (int q = 0; q < 4; q++) { if (v[q] < 0) v[q] = 0; if (v[q] > 1) v[q] = 1; guardado[i][q] = v[q]; par[i][q] = v[q]; } nCapas++;
    }
    else if (sscanf(l, "esc=%u", &u) == 1) reqEsc = u % NESC;
    else if (sscanf(l, "ton=%u", &u) == 1) reqTon = u % 12;
    else if (sscanf(l, "scn=%u", &u) == 1) { reqEscena = u & 7; escena = u & 7; }
    else if (sscanf(l, "bpat=%u", &u) == 1) reqBPat = u & 7;
    else if (sscanf(l, "boct=%u", &u) == 1) reqBOct = u % 3;
    else if (sscanf(l, "gli=%u", &u) == 1) reqGlide = u & 1;
    else if (sscanf(l, "prog=%u", &u) == 1) reqProg = u % 6;
    else if (sscanf(l, "voic=%u", &u) == 1) reqVoic = u % 5;
    else if (sscanf(l, "prit=%u", &u) == 1) reqPadRit = u % 3;
    else if (sscanf(l, "mson=%u", &u) == 1) reqMSon = u & 3;
    else if (sscanf(l, "moct=%u", &u) == 1) reqMOct = u % 3;
    else if (sscanf(l, "modo=%u", &u) == 1) reqModo = u & 3;
    else if (sscanf(l, "alt=%u", &u) == 1) reqAlt = u % 3;
    else if (sscanf(l, "frz=%u", &u) == 1) reqFrz = u & 1;
    else if (sscanf(l, "auto=%u", &u) == 1) reqAuto = u & 1;
    else if (sscanf(l, "run=%u", &u) == 1) runMask = u & 0x3F;
    else if (sscanf(l, "btim=%u", &u) == 1) reqBTim = u % 3;
    else if (sscanf(l, "poli=%u", &u) == 1) reqPoli = u & 7;
    else if (sscanf(l, "gsz=%u", &u) == 1) reqGSize = u & 3;
    else if (sscanf(l, "garm=%u", &u) == 1) reqGArm = u & 3;
    else if (sscanf(l, "gfrz=%u", &u) == 1) reqGFrz = u & 1;
    else if (sscanf(l, "src=%u", &u) == 1) mSrcSel = u & 3;
    else if (sscanf(l, "dst=%u", &u) == 1) mDstSel = u % 7;
    else if (sscanf(l, "lfa=%u", &u) == 1) lfoShape[0] = u & 3;
    else if (sscanf(l, "lfb=%u", &u) == 1) lfoShape[1] = u & 3;
    else if (sscanf(l, "mseg=%u", &u) == 1) msegSel = u & 7;
    else if (!strncmp(l, "mat=", 4)) { const char *q = l + 4; for (int a = 0; a < 4; a++) for (int d = 0; d < 7; d++) { char *e; float fv = strtof(q, &e); if (e == q) break; if (fv < -1) fv = -1; if (fv > 1) fv = 1; mAmt[a][d] = fv; q = (*e == ',') ? e + 1 : e; } }
  }
  if (nCapas < NCAPAS) return false;
  capaCtl = capa; for (int q = 0; q < 4; q++) { primeraLectura[q] = false; bloqueado[q] = false; entrada[q] = true; }
  guardado[6][2] = (mAmt[mSrcSel & 3][mDstSel % 7] + 1.0f) * 0.5f; par[6][2] = guardado[6][2];
  return true;
}
#define SD_SCK  14
#define SD_MISO 16
#define SD_MOSI 15
#define SD_CS   17
SPIClass sdSpi(FSPI); SemaphoreHandle_t sdMx; uint32_t sdFrecOk = 0;
bool sdMontar() {
  static const uint32_t FREC[4] = { 20000000, 10000000, 4000000, 1000000 };
  for (int i = 0; i < 4; i++) {
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, sdSpi, FREC[i])) { sdFrecOk = FREC[i]; if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR); return true; }
    SD.end(); sdSpi.end();
  }
  return false;
}
void sdIniciar() {
  if (!sdMontar()) { g_sd = 0; return; }
  g_sd = 1;
  File f = SD.open(SD_ARCH);
  if (f) { size_t n = f.read((uint8_t *)escenaBuf, sizeof(escenaBuf) - 1); escenaBuf[n] = 0; f.close(); if (aplicarTexto(escenaBuf)) { g_sd = 3; escenaCargada = true; } }
  armarEscena(escenaGuard, sizeof(escenaGuard)); strcpy(escenaUlt, escenaGuard);
  if (g_sd == 1) escenaGuard[0] = 0;
}
void sdTask(void *) {
  unsigned long tCambio = 0;
  for (;;) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (!g_sd) { vTaskDelay(4500 / portTICK_PERIOD_MS); xSemaphoreTake(sdMx, portMAX_DELAY); bool ok = sdMontar(); xSemaphoreGive(sdMx); if (ok) { g_sd = 1; escenaGuard[0] = 0; } continue; }
    armarEscena(escenaBuf, sizeof(escenaBuf));
    if (strcmp(escenaBuf, escenaUlt) != 0) { strcpy(escenaUlt, escenaBuf); tCambio = millis(); }
    else if (strcmp(escenaBuf, escenaGuard) != 0 && millis() - tCambio > 3000) {
      bool ok = false;
      xSemaphoreTake(sdMx, portMAX_DELAY);
      File f = SD.open(SD_TMP, FILE_WRITE);
      if (f) { size_t n = strlen(escenaBuf); ok = (f.write((const uint8_t *)escenaBuf, n) == n); f.close(); }
      if (ok) { SD.remove(SD_ARCH); ok = SD.rename(SD_TMP, SD_ARCH); }
      xSemaphoreGive(sdMx);
      if (ok) { strcpy(escenaGuard, escenaBuf); g_sd = (g_sd == 3) ? 3 : 1; } else { g_sd = 0; SD.end(); }
    }
  }
}

// ==============================================================================================================================================
// LEDs
// ==============================================================================================================================================
// Los LED se escriben con el RMT del núcleo de Arduino (rmtWrite, bloqueante), NO con FastLED.show(): el motor de canales de FastLED se colgaba
// 1 s (varias veces) cada vez que la pantalla hacía una transacción I2C, y eso congelaba botones y perillas. FastLED sólo se usa para CRGB/CHSV.
static rmt_data_t rmtBuf[NUM_LEDS * 24 + 1], rmtBuf1[24 + 1];
static void ledCodificar(rmt_data_t *d, uint8_t g, uint8_t r, uint8_t b) {
  uint32_t v = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  for (int i = 0; i < 24; i++) {
    bool uno = (v >> (23 - i)) & 1;
    d[i].level0 = 1; d[i].duration0 = uno ? 8 : 4; d[i].level1 = 0; d[i].duration1 = uno ? 5 : 9;      // 10 MHz: 0,8/0,45 µs (1) · 0,4/0,9 µs (0)
  }
}
static void ledsEscribir() {
  for (int i = 0; i < NUM_LEDS; i++) ledCodificar(&rmtBuf[i * 24], (uint8_t)((leds[i].g * LED_BRIGHT) >> 8), (uint8_t)((leds[i].r * LED_BRIGHT) >> 8), (uint8_t)((leds[i].b * LED_BRIGHT) >> 8));
  rmtWrite(LED_PIN, rmtBuf, NUM_LEDS * 24, 20);
  ledCodificar(rmtBuf1, onboard[0].g, onboard[0].r, onboard[0].b);
  rmtWrite(ONBOARD_PIN, rmtBuf1, 24, 20);
}
float readPot(uint8_t pin) { uint32_t s = 0; for (int i = 0; i < 4; i++) s += analogRead(pin); return (float)(s >> 2) / 4095.0f; }

uint8_t oledAddr_unused;
uint8_t oledAddr = 0x3C;
uint8_t fb[128 * 8];
const uint8_t FONT5[62][5] = {                                       // ASCII 0x20..0x5D, columnas, bit 0 = fila superior
  {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,8,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0,5,3,0,0},
  {0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},{0x2A,0x1C,0x7F,0x1C,0x2A},{8,8,0x3E,8,8},{0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
  {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
  {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0,0x7F,0x41,0x41,0},{0x02,0x04,0x08,0x10,0x20},{0,0x41,0x41,0x7F,0}
};
void oPx(int x, int y, bool on) {
  if (x < 0 || x > 127 || y < 0 || y > 63) return;
  if (on) fb[x + (y >> 3) * 128] |= (1 << (y & 7)); else fb[x + (y >> 3) * 128] &= ~(1 << (y & 7));
}
void oRect(int x, int y, int w, int h, bool on) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) oPx(x + i, y + j, on); }
void oChar(int x, int y, char c, int esc, bool inv) {
  if (c >= 'a' && c <= 'z') c -= 32;
  uint8_t col[5] = { 0, 0, 0, 0, 0 };
  if (c == '|') { col[2] = 0x7F; }
  else if (c >= 0x20 && c <= 0x5D) memcpy(col, FONT5[c - 0x20], 5);
  for (int i = 0; i < 6; i++) for (int j = 0; j < 8; j++) {
    bool on = (i < 5 && j < 7) ? ((col[i] >> j) & 1) : false;
    if (inv) on = !on;
    if (on) oRect(x + i * esc, y + j * esc, esc, esc, true);
  }
}
void oText(int x, int y, const char *t, int esc = 1, bool inv = false) { for (; *t; t++, x += 6 * esc) oChar(x, y, *t, esc, inv); }
void oTextC(int y, const char *t, int esc) { oText((128 - (int)strlen(t) * 6 * esc + esc) / 2, y, t, esc); }
void oCmd(uint8_t c) { Wire.beginTransmission(oledAddr); Wire.write((uint8_t)0x00); Wire.write(c); Wire.endTransmission(); }
bool oInit() {
  Wire.begin(21, 38); Wire.setClock(400000); Wire.setTimeOut(5);        // timeout corto: un cable flojo no puede congelar nada por segundos
  Wire.beginTransmission(0x3C); if (Wire.endTransmission() == 0) oledAddr = 0x3C;
  else { Wire.beginTransmission(0x3D); if (Wire.endTransmission() == 0) oledAddr = 0x3D; else return false; }
  static const uint8_t seq[] = { 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF };
  for (uint8_t c : seq) oCmd(c);
  return true;
}
uint8_t fbPrev[128 * 8];
// Sólo se manda a la pantalla lo que cambió, página por página (8 páginas de 128 bytes): un cambio pequeño cuesta ~4 ms en vez de ~35 ms,
// y entre páginas se cede la CPU. Devuelve false si la pantalla no contesta (cable flojo, etc.).
bool oFlush() {
  for (int pg = 0; pg < 8; pg++) {
    if (memcmp(&fb[pg * 128], &fbPrev[pg * 128], 128) == 0) continue;
    uint8_t cmd[6] = { 0x21, 0, 127, 0x22, (uint8_t)pg, (uint8_t)pg };
    Wire.beginTransmission(oledAddr); Wire.write((uint8_t)0x00); Wire.write(cmd, 6); if (Wire.endTransmission() != 0) { return false; }
    for (int i = 0; i < 128; i += 64) { Wire.beginTransmission(oledAddr); Wire.write((uint8_t)0x40); Wire.write(&fb[pg * 128 + i], 64); if (Wire.endTransmission() != 0) { return false; } }
    memcpy(&fbPrev[pg * 128], &fb[pg * 128], 128);
    vTaskDelay(1);
  }
  return true;
}

static inline bool pistaCorre(int c) { return c < 4 ? ((runMask >> c) & 1) : (c == 5 ? ((runMask >> 4) & 1) : (c == 6 ? ((runMask >> 5) & 1) : ((runMask & 0x1F) != 0))); }
void renderLEDs() {
  static unsigned long lastFrame = 0;
  unsigned long t = millis();
  if (t - lastFrame < LED_REFRESH_MS) return;
  lastFrame = t;
  int capa = capaCtl; uint8_t hu = HUE_CAPA[capa];
  bool medidor = (meterT != 0 && t - meterT < 900);
  if (medidor) {
    float n = meterV * 5.0f;
    for (int i = 0; i < 5; i++) { float l = n - (float)i; if (l < 0) l = 0; if (l > 1) l = 1; leds[i] = CHSV(meterHue, 255, (uint8_t)(6 + l * 240.0f)); }
  } else {
    leds[0] = CHSV(hu, 255, (uint8_t)(40 + g_pul[capa] * 200.0f));
    leds[1] = CHSV(hu, 255, (uint8_t)(12 + (capa < 4 ? g_niv[capa] : cl01((g_niv[0] + g_niv[1] + g_niv[2] + g_niv[3]) * 0.5f)) * 240.0f));
    if (capa == 0) { leds[2] = CHSV((uint8_t)(bPat * 32), 255, 110); leds[3] = CHSV(0, bOct == 1 ? 0 : 255, bOct == 1 ? 30 : 160); leds[4] = CHSV((uint8_t)(bTim * 60 + 5), 255, 150); }
    else if (capa == 1) { leds[2] = CHSV((uint8_t)(prog * 40), 255, 110); leds[3] = CHSV((uint8_t)(voic * 50 + 20), 255, 110); leds[4] = padRit ? CHSV((uint8_t)(padRit * 40), 255, 160) : CRGB(3, 3, 0); }
    else if (capa == 2) { leds[2] = CHSV((uint8_t)(mSon * 60), 255, 120); leds[3] = CHSV(190, 255, (uint8_t)(10 + 200.0f * par[2][3])); leds[4] = CHSV(0, mOct == 1 ? 0 : 255, mOct == 1 ? 30 : 160); }
    else if (capa == 3) { leds[2] = CHSV((uint8_t)(modo * 60 + 10), 255, 120); leds[3] = CHSV(0, alt == 1 ? 0 : 255, alt == 1 ? 30 : 160); leds[4] = frz ? CRGB(120, 200, 255) : CRGB(2, 3, 4); }
    else if (capa == 5) { leds[2] = CHSV((uint8_t)(reqGArm * 50 + 10), 255, 120); leds[3] = CHSV((uint8_t)(reqGSize * 60 + 30), 255, 120); leds[4] = reqGFrz ? CRGB(120, 200, 255) : CRGB(2, 3, 4); }
    else if (capa == 6) { float am = mAmt[mSrcSel & 3][mDstSel % 7]; leds[2] = CHSV((uint8_t)(mSrcSel * 60), 255, 130); leds[3] = CHSV((uint8_t)(mDstSel * 36 + 10), 255, 130); leds[4] = am >= 0 ? CRGB(0, (uint8_t)(10 + 200.0f * am), 0) : CRGB((uint8_t)(10 - 200.0f * am), 0, 0); }
    else { leds[2] = CHSV((uint8_t)(g_esc * 32), 255, 110); leds[3] = CHSV((uint8_t)(g_ton * 21), 255, 110); leds[4] = CHSV((uint8_t)(g_escena * 32 + 8), 255, 110); }
  }
  if (!medidor && !pistaCorre(capa)) leds[0] = CRGB(40, 0, 0);
  if (menuAbierto) { for (int i = 0; i < 5; i++) leds[i] = ((t / 200 + i) & 1) ? CHSV(200, 255, 200) : CHSV(200, 255, 20); }
  if (g_sinRecoger) leds[5] = CRGB(255, 120, 0);
  else if (g_pend) leds[5] = ((t / 180) & 1) ? CRGB(120, 0, 160) : CRGB(10, 0, 14);     // cambio armado: entra en el próximo compás
  else leds[5] = g_sd ? CRGB(0, 8, 0) : CRGB(8, 8, 8);
  if (xrunAt != 0 && t - xrunAt < 1500) leds[5] = CRGB(255, 0, 0);
  onboard[0] = CHSV(hu, 255, 20 + (uint8_t)(g_niv[0] * 100.0f + g_niv[2] * 100.0f)); onboard[0].nscale8(ONBOARD_BRIGHT);
  if (flashLevel > 0.02f) { uint8_t w = (uint8_t)(flashLevel * 110.0f); for (int i = 0; i < NUM_LEDS; i++) leds[i] += CRGB(w, w, w); flashLevel *= 0.62f; }
  ledsEscribir();
}

static const char *POT_NOM[NCAPAS][4] = { { "VOLUMEN", "REJILLA", "FILTRO", "CUERPO" }, { "VOLUMEN", "CAMBIO", "FILTRO", "ATAQUE" }, { "VOLUMEN", "REJILLA", "BRILLO", "DENSIDAD" },
  { "VOLUMEN", "DECAIMIENTO", "ENTRADA", "TONO" }, { "MASTER", "TEMPO", "SWING", "ENVIOS" }, { "VOLUMEN", "DENSIDAD", "FILTRO", "DISPERSION" }, { "PROFUND.", "RITMO LFO", "CANTIDAD", "LARGO MSEG" } };
static const char *BTN_NOM[NCAPAS][3] = { { "PATRON", "OCTAVA", "TIMBRE" }, { "PROGRES.", "VOICING", "RITMO" }, { "SEMILLA", "SONIDO", "OCTAVA" }, { "MODO", "ALTURA", "CONGELA" }, { "ESCALA", "TONICA", "POLI" }, { "CONGELA", "GRANO", "ARMONIA" }, { "FUENTE", "DESTINO", "FORMA" } };
static const char *REJ_NOM[3] = { "1/4", "1/8", "1/16" };
void oDibujar() {
  memset(fb, 0, sizeof(fb));
  unsigned long t = millis(); int capa = capaCtl; char b[40];
  bool run = pistaCorre(capa);
  if (menuAbierto) {                                                  // MENÚ DE ESCENAS (BTN1 mantenido)
    oRect(0, 0, 128, 9, true); oText(2, 1, "ESCENAS", 1, true);
    snprintf(b, sizeof(b), "AUTO %s", reqAuto ? "SI" : "NO"); oText(128 - 2 - (int)strlen(b) * 6, 1, b, 1, true);
    int ini = menuSel - 2; if (ini > 3) ini = 3; if (ini < 0) ini = 0;
    for (int r = 0; r < 5; r++) {
      int i = ini + r; bool sel = (i == menuSel);
      if (sel) oRect(0, 11 + r * 9, 128, 9, true);
      snprintf(b, sizeof(b), "%c%d %.9s %s%s", sel ? '>' : ' ', i + 1, ESCENAS[i].nom, ESC_COR[ESCENAS[i].esc], i == escena ? " <" : ""); oText(2, 12 + r * 9, b, 1, sel);
    }
    oRect(0, 54, 128, 10, true); oText(2, 56, "B3/4 ELIGE B5 LANZA B2 AUTO", 1, true);
    return;
  }
  if (t - capaT < 1000 && capaT != 0) {                               // anuncio de capa
    snprintf(b, sizeof(b), "PISTA %d DE %d", posDe(capa) + 1, NORD); oTextC(4, b, 2);
    oTextC(26, CAPA_NOM[capa], strlen(CAPA_NOM[capa]) > 7 ? 2 : 3);
    oRect(0, 52, 128, 1, true);
    if (escenaCargada && t < 4500) snprintf(b, sizeof(b), "ESCENA CARGADA DE SD"); else snprintf(b, sizeof(b), "%s  %s %s", run ? "ACTIVA" : "APAGADA", ESC_COR[g_esc], TON_NOM[g_ton]);
    oTextC(55, b, 1);
    return;
  }
  oRect(0, 0, 128, 9, true);
  snprintf(b, sizeof(b), "%d/%d %s", posDe(capa) + 1, NORD, CAPA_NOM[capa]); oText(2, 1, b, 1, true);
  snprintf(b, sizeof(b), "%s %s", ESC_COR[g_esc], TON_NOM[g_ton]); oText(128 - 2 - (int)strlen(b) * 6, 1, b, 1, true);
  oRect(0, 9, 128, 1, false); oRect(0, 9, (g_paso + 1) * 8, 1, true);   // posición dentro del compás (16 pasos)
  for (int p = 0; p < 4; p++) {
    int y = 11 + p * 9; float v = guardado[capa][p];
    snprintf(b, sizeof(b), "P%d %s", p + 1, POT_NOM[capa][p]); oText(0, y, b);
    oRect(72, y + 1, 28, 6, true); oRect(73, y + 2, 26, 4, false); oRect(73, y + 2, (int)(26.0f * v + 0.5f), 4, true);
    if (!bloqueado[p]) { int xk = 73 + (int)(25.0f * potUlt[p]); oRect(xk, y - 1, 1, 9, true); }
    if (p == 1 && capa == 4) snprintf(b, sizeof(b), "%d", (int)(70.0f + 90.0f * v + 0.5f));
    else if (p == 1 && (capa == 0 || capa == 2)) snprintf(b, sizeof(b), "%s", REJ_NOM[v < 0.34f ? 0 : (v < 0.67f ? 1 : 2)]);
    else if (p == 1 && capa == 1) snprintf(b, sizeof(b), "%dC", v < 0.34f ? 1 : (v < 0.67f ? 2 : 4));
    else if (p == 1 && capa == 6) { static const char *BN[6] = { "16B", "8B", "4B", "2B", "1B", "1/2B" }; snprintf(b, sizeof(b), "%s", BN[(int)(v * 5.999f)]); }
    else if (p == 3 && capa == 6) snprintf(b, sizeof(b), "%dC", v < 0.34f ? 1 : (v < 0.67f ? 2 : 4));
    else if (p == 2 && capa == 6) snprintf(b, sizeof(b), "%+d", (int)((v * 2.0f - 1.0f) * 100.0f));
    else snprintf(b, sizeof(b), "%d", (int)(v * 100.0f + 0.5f));
    oText(128 - (int)strlen(b) * 6, y, b);
  }
  char e3[9], e4[9], e5[9];
  snprintf(e3, sizeof(e3), "%.7s", BTN_NOM[capa][0]); snprintf(e4, sizeof(e4), "%.7s", BTN_NOM[capa][1]); snprintf(e5, sizeof(e5), "%.7s", BTN_NOM[capa][2]);
  if (capa == 0) { snprintf(e3, sizeof(e3), "%.7s", BPAT_NOM[bPat]); snprintf(e4, sizeof(e4), "OCT %s", bOct == 0 ? "-1" : (bOct == 1 ? "0" : "+1")); snprintf(e5, sizeof(e5), "%.7s", BTIM_NOM[bTim]); }
  else if (capa == 1) { snprintf(e3, sizeof(e3), "PROG %d", prog + 1); snprintf(e4, sizeof(e4), "%.7s", VOIC_NOM[voic]); snprintf(e5, sizeof(e5), "%.7s", PRIT_NOM[padRit]); }
  else if (capa == 2) { snprintf(e4, sizeof(e4), "%.7s", MSON_NOM[mSon]); snprintf(e5, sizeof(e5), "OCT %s", mOct == 0 ? "-1" : (mOct == 1 ? "0" : "+1")); }
  else if (capa == 3) { snprintf(e3, sizeof(e3), "%.7s", MODO_NOM[modo]); snprintf(e4, sizeof(e4), "ALT %s", alt == 0 ? "-1" : (alt == 1 ? "0" : "+1")); snprintf(e5, sizeof(e5), "CONG%c", frz ? '+' : '-'); }
  else if (capa == 5) { snprintf(e3, sizeof(e3), "CONG%c", reqGFrz ? '+' : '-'); snprintf(e4, sizeof(e4), "%.7s", GSIZE_NOM[reqGSize & 3]); snprintf(e5, sizeof(e5), "%.7s", GARM_NOM[reqGArm & 3]); }
  else if (capa == 6) { snprintf(e3, sizeof(e3), "%.7s", SRC_NOM[mSrcSel & 3]); snprintf(e4, sizeof(e4), "%.7s", DST_NOM[mDstSel % 7]); snprintf(e5, sizeof(e5), "%.7s", (mSrcSel & 3) < 2 ? LFO_NOM[lfoShape[mSrcSel & 1] & 3] : ((mSrcSel & 3) == 2 ? MSEG_NOM[msegSel & 7] : "SIN FORM")); }
  else { snprintf(e5, sizeof(e5), "POLI %s", POLI_NOM[reqPoli & 7]); }
  snprintf(b, sizeof(b), "B2 %-4s B3 %-7s", run ? "STOP" : "PLAY", e3); oText(0, 47, b);
  snprintf(b, sizeof(b), "B4 %-7s B5 %-7s", e4, e5); oText(0, 55, b);
  if (g_pend) { oRect(88, 1, 34, 7, false); oText(90, 1, "ARMADO", 1, false); }              // hay un cambio esperando el próximo compás
  if (toastT != 0 && t - toastT < 1500) { oRect(0, 54, 128, 10, true); oText(2, 56, toastBuf, 1, true); }
}
void oledTask(void *) {
  bool hay = oInit();
  if (!hay) vTaskDelete(NULL);
  memset(fbPrev, 0xA5, sizeof(fbPrev));
  int fallos = 0;
  for (;;) {
    oDibujar();
    bool ok = oFlush();
    if (ok) fallos = 0; else if (++fallos >= 3) { fallos = 0; vTaskDelay(3000 / portTICK_PERIOD_MS); memset(fbPrev, 0xA5, sizeof(fbPrev)); oInit(); }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

static void sincMat() { guardado[6][2] = (mAmt[mSrcSel & 3][mDstSel % 7] + 1.0f) * 0.5f; par[6][2] = guardado[6][2]; bloqueado[2] = false; entrada[2] = true; }   // la perilla CANTIDAD se reenlaza con la ruta elegida sin saltar
bool bL[5] = { HIGH, HIGH, HIGH, HIGH, HIGH }; unsigned long bT[5] = { 0, 0, 0, 0, 0 };
void pasoControl() {
  unsigned long t = millis();
#if DIAG
  { static unsigned long ult = 0; if (ult) { unsigned long d = t - ult; if (d > dgCtlMax) dgCtlMax = d; } ult = t; }
#endif
  bool b[5];
#if DIAG
  b[0] = HIGH;
#else
  b[0] = digitalRead(BTN1_PIN);
#endif
  b[1] = digitalRead(BTN2_PIN); b[2] = digitalRead(BTN3_PIN); b[3] = digitalRead(BTN4_PIN); b[4] = digitalRead(BTN5_PIN);
  bool f[5];
  for (int i = 0; i < 5; i++) { f[i] = (b[i] == LOW && bL[i] == HIGH && t - bT[i] > DEBOUNCE_MS); if (f[i]) bT[i] = t; }
  // REGLAS FIJAS: BTN1 corto = pista siguiente · mantenido 0,7 s = menú de escenas · POT1 volumen · BTN2 stop/play · POT2 tempo/rejilla de la pista
  static unsigned long b1T = 0; static bool b1Largo = false;
  if (f[0]) { b1T = t; b1Largo = false; }
  if (b[0] == LOW && b1T && !b1Largo && t - b1T > 700) { b1Largo = true; menuAbierto = true; menuSel = g_escena; flashLevel = 0.8f; }
  if (b[0] == HIGH && bL[0] == LOW && b1T) { if (!b1Largo) { if (menuAbierto) menuAbierto = false; else cambiarCapa(ORDEN[(posDe(capaCtl) + 1) % NORD]); } b1T = 0; }
  int capa = capaCtl;
  if (menuAbierto) {
    if (f[2] && menuSel > 0) menuSel--;
    if (f[3] && menuSel < 7) menuSel++;
    if (f[4]) { reqEscena = menuSel; reqAuto = reqAuto; flashLevel = 0.8f; toast("ESCENA %s (SIG. COMPAS)", ESCENAS[menuSel].nom); menuAbierto = false; }
    if (f[1]) { reqAuto ^= 1; toast("AUTO %s", reqAuto ? "SI" : "NO"); }
  } else {
    if (f[1]) {
      if (capa < 4) runMask = runMask ^ (uint8_t)(1 << capa); else if (capa == 5) runMask = runMask ^ 16; else if (capa == 6) runMask = runMask ^ 32; else runMask = (runMask & 0x1F) ? (runMask & 0x20) : (runMask | 0x1F);
      flashLevel = 0.5f; toast("%s %s", CAPA_NOM[capa], pistaCorre(capa) ? "ACTIVA" : "APAGADA");
    }
    if (capa == 0) {
      if (f[2]) { reqBPat = (reqBPat + 1) & 7; toast("PATRON %s", BPAT_NOM[reqBPat]); flashLevel = 0.4f; }
      if (f[3]) { reqBOct = (reqBOct + 1) % 3; toast("OCTAVA %s", reqBOct == 0 ? "-1" : (reqBOct == 1 ? "0" : "+1")); flashLevel = 0.4f; }
      if (f[4]) { reqBTim = (reqBTim + 1) % 3; toast("TIMBRE %s (SIG. COMPAS)", BTIM_NOM[reqBTim]); flashLevel = 0.4f; }
    } else if (capa == 1) {
      if (f[2]) { reqProg = (reqProg + 1) % 6; toast("PROGRESION %d", reqProg + 1); flashLevel = 0.4f; }
      if (f[3]) { reqVoic = (reqVoic + 1) % 5; toast("VOICING %s", VOIC_NOM[reqVoic]); flashLevel = 0.4f; }
      if (f[4]) { reqPadRit = (reqPadRit + 1) % 3; toast("RITMO %s", PRIT_NOM[reqPadRit]); flashLevel = 0.4f; }
    } else if (capa == 2) {
      if (f[2]) { reqMGen = reqMGen + 1; toast("SEMILLA NUEVA"); flashLevel = 0.4f; }
      if (f[3]) { reqMSon = (reqMSon + 1) & 3; toast("SONIDO %s", MSON_NOM[reqMSon]); flashLevel = 0.4f; }
      if (f[4]) { reqMOct = (reqMOct + 1) % 3; toast("OCTAVA %s", reqMOct == 0 ? "-1" : (reqMOct == 1 ? "0" : "+1")); flashLevel = 0.4f; }
    } else if (capa == 3) {
      if (f[2]) { reqModo = (reqModo + 1) & 3; toast("MODO %s", MODO_NOM[reqModo]); flashLevel = 0.4f; }
      if (f[3]) { reqAlt = (reqAlt + 1) % 3; toast("ALTURA %s", reqAlt == 0 ? "-1" : (reqAlt == 1 ? "0" : "+1")); flashLevel = 0.4f; }
      if (f[4]) { reqFrz ^= 1; toast("CONGELA %s", reqFrz ? "SI" : "NO"); flashLevel = 0.4f; }
    } else if (capa == 5) {
      if (f[2]) { reqGFrz ^= 1; toast("TEXTURA CONGELA %s", reqGFrz ? "SI" : "NO"); flashLevel = 0.4f; }
      if (f[3]) { reqGSize = (reqGSize + 1) & 3; toast("GRANO %s", GSIZE_NOM[reqGSize]); flashLevel = 0.4f; }
      if (f[4]) { reqGArm = (reqGArm + 1) & 3; toast("ARMONIA %s", GARM_NOM[reqGArm]); flashLevel = 0.4f; }
    } else if (capa == 6) {
      if (f[2]) { mSrcSel = (mSrcSel + 1) & 3; sincMat(); toast("FUENTE %s", SRC_NOM[mSrcSel]); flashLevel = 0.4f; }
      if (f[3]) { mDstSel = (mDstSel + 1) % 7; sincMat(); toast("DESTINO %s", DST_NOM[mDstSel]); flashLevel = 0.4f; }
      if (f[4]) {
        if (mSrcSel < 2) { lfoShape[mSrcSel] = (lfoShape[mSrcSel] + 1) & 3; toast("%s %s", SRC_NOM[mSrcSel], LFO_NOM[lfoShape[mSrcSel]]); }
        else if (mSrcSel == 2) { msegSel = (msegSel + 1) & 7; toast("MSEG %s", MSEG_NOM[msegSel]); }
        else toast("ENV: SIN FORMA");
        flashLevel = 0.4f;
      }
    } else {
      if (f[2]) { reqEsc = (reqEsc + 1) % NESC; toast("ESCALA %s", ESC_NOM[reqEsc]); flashLevel = 0.4f; }
      if (f[3]) { reqTon = (reqTon + 7) % 12; toast("TONICA %s", TON_NOM[reqTon]); flashLevel = 0.4f; }
      if (f[4]) { reqPoli = (reqPoli + 1) & 7; toast("POLI %s (BAJO/MEL)", POLI_NOM[reqPoli]); flashLevel = 0.5f; }
    }
  }
  for (int i = 0; i < 5; i++) bL[i] = b[i];
  static const uint8_t POT_PIN[4] = { POT1, POT2, POT3, POT4 };
  static uint8_t scan = 0; int pi = scan; scan = (scan + 1) & 3;
  float v = readPot(POT_PIN[pi]);
  if (potUlt[pi] < 0.0f || fabsf(v - potUlt[pi]) > 0.004f) potUlt[pi] = v;
  leerPotCapa(pi, potUlt[pi]);
#if DIAG && defined(FUZZ)
  { static unsigned long tf = 0; if (t - tf > 700) { tf = t; uint32_t r = xr();
      reqEsc = xr() % NESC; reqTon = xr() % 12; reqEscena = xr() & 7; reqBPat = xr() & 7; reqBOct = xr() % 3; reqGlide = xr() & 1; reqProg = xr() % 6; reqVoic = xr() % 5; reqPadRit = xr() & 1;
      reqMSon = xr() % 5; reqMOct = xr() % 3; reqModo = xr() & 3; reqAlt = xr() % 3; reqFrz = (r >> 5) & 1; runMask = (uint8_t)(xr() | 1) & 0x3F; reqBTim = xr() % 3; reqPoli = xr() & 7; reqGSize = xr() & 3; reqGArm = xr() & 3; reqGFrz = (r >> 9) & 1; mSrcSel = xr() & 3; mDstSel = xr() % 7; lfoShape[0] = xr() & 3; lfoShape[1] = xr() & 3; msegSel = xr() & 7; for (int a2 = 0; a2 < 4; a2++) for (int d2 = 0; d2 < 7; d2++) mAmt[a2][d2] = ((float)(xr() % 2001) - 1000.0f) / 1000.0f; if ((r & 7) == 0) reqMGen = reqMGen + 1;
      for (int c = 0; c < NCAPAS; c++) for (int p = 0; p < 4; p++) par[c][p] = (c == 4 && p == 0) ? 0.8f : (float)(xr() % 1000) / 999.0f; } }
#endif
#if !DIAG
  while (mqT != mqH && Serial1.availableForWrite() > 0) { Serial1.write(midiQ[mqT]); mqT = (mqT + 1) & 511; }
  { static unsigned long tb = 0; if (t - tb > 30) { tb = t;                    // MPE: inflexión y timbre PROPIOS de cada nota del acorde
      for (int j = 0; j < 4; j++) if (padMidi[j] >= 0 && Serial1.availableForWrite() > 8) {
        int bend = 8192 + (int)(g_dr[j] * 25.6f); uint8_t ch = (uint8_t)(1 + j);
        Serial1.write(0xE0 | ch); Serial1.write(bend & 127); Serial1.write((bend >> 7) & 127);
        Serial1.write(0xB0 | ch); Serial1.write(74); Serial1.write((uint8_t)(64 + (int)((g_tim[j] - 0.5f) * 100.0f)));
      } } }
#endif
  renderLEDs();
#if DIAG
  static unsigned long lastP = 0;
  if (t - lastP > 2000) {
    lastP = t;
    DBG_PRINTF("[%lus] DIAG render promedio %u us (%.0f %%)  maximo %u us (%.0f %%)  retrasos %u | pico %.2f sobre0.75 %u | esc %s ton %s escena %d acorde %d bpm %.0f ctlmax %lu curaciones %u pend %d\n",
               (unsigned long)(t / 1000), (unsigned)g_usAvg, 100.0f * g_usAvg / BUF_US, (unsigned)g_usMax, 100.0f * g_usMax / BUF_US, (unsigned)g_xruns, dgPk, (unsigned)dgSobre,
               ESC_COR[g_esc], TON_NOM[g_ton], (int)g_escena, (int)g_acorde, g_bpm, dgCtlMax, (unsigned)g_heal, (int)g_pend);
    g_usMax = 0; dgPk = 0; dgSobre = 0; dgCtlMax = 0;
  }
#endif
}

void audioTask(void *);
void controlTask(void *);
void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
#if DIAG
  Serial0.begin(115200);
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
  delay(300); DBG_PRINTF("BOOT motivo de reinicio=%d\n", (int)esp_reset_reason());
#endif
  pinMode(BTN1_PIN, INPUT_PULLUP); pinMode(BTN2_PIN, INPUT_PULLUP); pinMode(BTN3_PIN, INPUT_PULLUP); pinMode(BTN4_PIN, INPUT_PULLUP); pinMode(BTN5_PIN, INPUT_PULLUP);
  analogReadResolution(12); analogSetAttenuation(ADC_11db);
  for (int i = 0; i <= 1024; i++) SIN[i] = sinf(2.0f * (float)M_PI * (float)(i & 1023) / 1024.0f);
  construirTablas();
  for (int fr = 0; fr < 8; fr++) {                                      // tabla de ondas: 8 cuadros con ≤ 20 armónicos (sin alias hasta ~1,1 kHz de fundamental)
    float pk = 1e-9f;
    for (int i = 0; i < 1024; i++) {
      float th = 2.0f * (float)M_PI * (float)i / 1024.0f, v = 0;
      for (int n = 1; n <= 20; n++) {
        float a = 0, nn = (float)n;
        switch (fr) {
          case 0: a = (n == 1) ? 1.0f : 0.0f; break;
          case 1: a = (n & 1) ? ((((n - 1) / 2) & 1) ? -1.0f : 1.0f) / (nn * nn) : 0.0f; break;
          case 2: a = 1.0f / nn; break;
          case 3: a = (n & 1) ? 1.0f / nn : 0.0f; break;
          case 4: a = 1.0f / powf(nn, 0.7f); break;
          case 5: a = (1.0f / nn) * (0.2f + expf(-(nn - 5.0f) * (nn - 5.0f) / 4.5f) * 3.0f + expf(-(nn - 13.0f) * (nn - 13.0f) / 8.0f) * 1.8f); break;
          case 6: a = (1.0f / nn) * (0.2f + expf(-(nn - 3.0f) * (nn - 3.0f) / 3.0f) * 3.0f + expf(-(nn - 8.0f) * (nn - 8.0f) / 6.0f) * 1.5f); break;
          default: a = (n <= 16) ? 1.0f / powf(nn, 0.3f) : 0.0f; break;
        }
        v += a * sinf(nn * th);
      }
      WT[fr][i] = v; if (fabsf(v) > pk) pk = fabsf(v);
    }
    for (int i = 0; i < 1024; i++) WT[fr][i] *= 0.9f / pk;
    WT[fr][1024] = WT[fr][0];
  }
  memcpy(WTcur, WT[2], sizeof(WTcur));
  gBuf2 = (int16_t *)heap_caps_calloc(G2_N, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!gBuf2) gBuf2 = (int16_t *)heap_caps_calloc(G2_N, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  memset(mAmt, 0, sizeof(mAmt));                                        // rutas de fábrica (se pueden cambiar en MATRIZ y se guardan en la SD)
  mAmt[0][2] = 0.5f; mAmt[1][1] = 0.35f; mAmt[2][0] = 0.45f; mAmt[3][5] = -0.25f; mAmt[0][3] = 0.2f;
  ecoL = (int16_t *)heap_caps_malloc(ECO_N * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ecoR = (int16_t *)heap_caps_malloc(ECO_N * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  resetAudio();
  rmtInit(LED_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  rmtInit(ONBOARD_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
  onboard[0] = CRGB::Black; ledsEscribir();
  for (int c = 0; c < NCAPAS; c++) for (int p = 0; p < 4; p++) par[c][p] = guardado[c][p];
  guardado[6][2] = (mAmt[0][0] + 1.0f) * 0.5f; par[6][2] = guardado[6][2];
  sdMx = xSemaphoreCreateMutex();
  sdIniciar();
#if !DIAG
  Serial1.begin(MIDI_BAUD, SERIAL_8N1, -1, 43);                        // MIDI OUT (MPE: acorde en canales 2-5, bajo 6, melodía 7)
  for (int c = 1; c <= 4; c++) { Serial1.write(0xB0 | c); Serial1.write(101); Serial1.write(0); Serial1.write(0xB0 | c); Serial1.write(100); Serial1.write(0); Serial1.write(0xB0 | c); Serial1.write(6); Serial1.write(48); Serial1.write(0xB0 | c); Serial1.write(38); Serial1.write(0); }
#endif
  rngS ^= (uint32_t)micros() * 2654435761u; if (rngS == 0) rngS = 2463534242u;
  melGenerar(); if (!escenaCargada) aplicarEscena(0);
  stepCnt = 1; stepPos = 0;
  i2s_init();
  xTaskCreatePinnedToCore(audioTask,   "audio",   8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 10240, NULL, 3, NULL, 0);
  capaT = millis();
  xTaskCreatePinnedToCore(oledTask,    "oled",    8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(sdTask,      "sd",      10240, NULL, 1, NULL, 0);
}
void audioTask(void *)   { for (;;) renderBuffer(); }
void controlTask(void *) { for (;;) { pasoControl(); vTaskDelay(1); } }
void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
