// ==============================================================================================================================================
// PERCU-SYNTH — piano piano (piano ambiente generativo + fondo industrial suave, todo en sintonía) — GC Lab Chile
// ==============================================================================================================================================
// HARDWARE
// ==============================================================================================================================================
// - ESP32-S3 (PSRAM OPI) + DAC PCM5102 (I2S: LCK 39, DIN 40, BCK 41) + 5 botones + 4 pots + 6 LEDs SMD WS2812
// - OLED SSD1306 128x64 (SDA 21, SCL 38) y módulo microSD SPI (SCK 14, MOSI 15, MISO 16, CS 17; FAT32)
// ==============================================================================================================================================
// ARDUINO IDE
// ==============================================================================================================================================
// - Board: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · PSRAM: OPI PSRAM · optimización -O2 (platform.local.txt:
//   compiler.optimization_flags=-O2) · Librerías: FastLED
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// Un piano muy suave que se toca solo en una escala (cambia de escala, de tónica y de estilo cuando tú quieras), un fondo industrial relajado
// (zumbido, golpe lento, metales y vapor) y una capa de ambiente (drón + nube de campanas). Todo comparte UNA armonía —tónica, escala y acorde
// en curso— y por eso está en sintonía: el zumbido es la tónica, el golpe cae en la tónica, los metales son notas del acorde con parciales
// armónicos, el vapor resuena en un armónico del acorde, el drón y las campanas salen de los mismos grados. El acorde cambia despacio solo.
//
// CINCO CAPAS (BTN1 corto = siguiente · BTN1 mantenido 0,7 s = menú de estados guardados · POT1 volumen · BTN2 stop/play · POT2 tempo):
//   1 PIANO       P3 toque (suave↔brillante) · P4 notas (densidad)        | B3 escala (8) · B4 tónica (por quintas) · B5 estilo (melodía/arpegio/acordes/mixto)
//   2 INDUSTRIAL  P3 densidad · P4 oscuridad                              | B3 pulso · B4 metal · B5 vapor
//   3 AMBIENTE    P3 brillo del drón · P4 nube de campanas                | B3 drón · B4 nube · B5 acorde del drón (diatónico/abierto/sus2/sus4)
//   4 EFECTOS     P3 reverb (tamaño y mezcla) · P4 eco (realimentación)   | B3 CONGELAR · B4 color (abierto/cálido/oscuro/claro) · B5 barrido (mantener; al soltar, impacto)
//   6 VIAJE       P2 duración (2-14 s) · P3 fuerza · P4 azar (0 = nada solo)  | B3 RUPTURA industrial · B4 tipo (subida/bajada/reversa/turbina) · B5 VIAJE
//   5 ESTADOS     P2 tempo global (×0,5…×2) · P3 tiempo de transición · P4 intensidad | B3 anterior · B4 siguiente · B5 sorteo
// Estados: 8 (CALMA · NIEBLA · FABRICA DISTANTE · NOCHE · DERIVA · CRISTAL · MAQUINA · SUSPENSO). Cambiar de estado desliza los valores en 2-60 s
// (nada salta). En el menú (BTN1 mantenido): BTN3/4 eligen, BTN5 carga, BTN2 guarda ahí tu mezcla actual.
// Todo se guarda solo en la microSD (/PIANO_PIANO/escena.txt, 3 s después del último cambio) y se recupera al encender.
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


// ==============================================================================================================================================
// ARMONÍA COMPARTIDA: UNA tónica, UNA escala y UN acorde en curso. Todo lo que suena con altura sale de aquí (por eso está en sintonía).
// ==============================================================================================================================================
#define NESC 8
static const char *ESC_NOM[NESC] = { "DORICO", "LIDIO", "EOLICO", "MIXOLIDIO", "PENTA MENOR", "PENTA MAYOR", "HIRAJOSHI", "INSEN" };
static const char *ESC_COR[NESC] = { "DORICO", "LIDIO", "EOLICO", "MIXOL", "PENT-", "PENT+", "HIRAJ", "INSEN" };
const uint8_t ESC_N[NESC] = { 7, 7, 7, 7, 5, 5, 5, 5 };
const int8_t ESC_I[NESC][7] = {
  { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 10 },
  { 0, 3, 5, 7, 10, 0, 0 }, { 0, 2, 4, 7, 9, 0, 0 }, { 0, 2, 3, 7, 8, 0, 0 }, { 0, 1, 5, 7, 10, 0, 0 } };
static const char *TON_NOM[12] = { "DO", "DO#", "RE", "MIB", "MI", "FA", "FA#", "SOL", "LAB", "LA", "SIB", "SI" };
static const char *TIPO_NOM[4] = { "TRIADA", "ABIERTO", "SUS2", "SUS4" };
static const char *EST_ESTILO[4] = { "MELODIA", "ARPEGIO", "ACORDES", "MIXTO" };
static const char *COL_NOM[4] = { "ABIERTO", "CALIDO", "OSCURO", "CLARO" };

#define NCAPAS 9
#define NV 28                                                         // valores por estado: 4 capas × 4 + ELECTRO (4) + INMERSION (4)
volatile float par[NCAPAS][4];                                        // control → audio: el valor de cada perilla de cada capa (0..1)
#define FL_PULSO 1
#define FL_METAL 2
#define FL_VAPOR 4
#define FL_DRON  8
#define FL_NUBE  16
#define FL_CONG  32
#define FL_BYP   64
#define FL_SW    (FL_PULSO | FL_METAL | FL_DRON | FL_NUBE)             // INTERRUPTORES de fuentes de sonido: sólo los mueve el usuario (ni un estado ni EVOLUCION los enciende)
#define FL_ORB   (1u << 16)                                           // INMERSION: órbita, shimmer, ensemble
#define FL_SHI   (1u << 17)
#define FL_ENS   (1u << 18)
#define ETIM_SH  12                                                   // bits 12-13: timbre de ELECTRO
#define EMOD_SH  14                                                   // bits 14-15: modo de ELECTRO
#define MCON     (1u << 19)                                           // MICRO: congelar lo capturado
#define MUSO_SH  20                                                   // bits 20-21: uso del micrófono (0 ambiente · 1 nube · 2 eco · 3 sólo control)
#define MINF     (1u << 22)                                           // MICRO: el nivel del micro influye en la música
#define FL_ECO2  (1u << 29)                                           // EFECTOS: eco a corchea en vez de corchea con puntillo
#define PTIM_SH  26                                                   // bits 26-28: sonido del piano (0 piano · 1 fieltro · 2 eléctrico · 3 caja · 4 vidrio · 5 grave)
#define ERIT_SH  24                                                   // bits 24-25: ritmo de ELECTRO (0 sin · 1 pulso · 2 roto · 3 denso)
#define TIPO_SH  8                                                    // bits 8-9: tipo de acorde del drón
#define COL_SH   10                                                   // bits 10-11: color del master
volatile uint32_t flagsA = FL_ENS;
volatile uint8_t reqEsc = 0, reqTon = 2, reqEstilo = 0, runMask = 0x01;  // bit0 piano · 1 industrial · 2 ambiente · 3 electro · 4 inmersión · 5 micro (al arrancar sólo suena el PIANO: lo demás lo enciendes tú)
volatile bool reqBarrido = false;
volatile uint32_t reqVaporN = 0;
volatile uint32_t reqRupN = 0, reqViajeN = 0, reqViajeStopN = 0; volatile uint8_t reqViajeTipo = 0;   // capa VIAJE: ruptura industrial y transiciones
volatile bool g_viaje = false;
volatile float g_pulE = 0, g_el = 0;
float cmpEnv = 0, cmpGan = 1, cmpGT = 1;
static const char *VIA_NOM[4] = { "SUBIDA", "BAJADA", "REVERSA", "TURBINA" };
static const char *ETIM_NOM[4] = { "SIERRA", "CUADRADA", "FM METAL", "RUIDO" };
static const char *EMOD_NOM[4] = { "BAJO", "ARPEGIO", "SECUENCIA", "PULSO" };
static const char *MUSO_NOM[4] = { "AMBIENTE", "NUBE", "ECO", "CONTROL" };
static const char *EVO_NOM[4] = { "OFF", "LENTA", "MEDIA", "RAPIDA" };
volatile uint8_t reqEvo = 0;                                         // EVOLUCION apagada al arrancar: nada cambia solo hasta que la enciendas (B5 en ESTADOS)
volatile float g_micEnv = 0;
static const char *ERIT_NOM[4] = { "SIN RITMO", "PULSO", "ROTO", "DENSO" };
volatile uint32_t reqElGenN = 0;
inline int capaDeIdx(int i) { return i < 16 ? (i >> 2) : (i < 20 ? 6 : (i < 24 ? 7 : 8)); }
volatile int g_acorde = 0, g_estilo = 0, g_esc = 0, g_ton = 2;
volatile float g_pian = 0, g_ind = 0, g_amb = 0, g_pul = 0, g_pulI = 0, g_pulA = 0;
volatile bool g_sinRecoger = false;
volatile int g_sd = 0;
volatile unsigned long xrunAt = 0;
volatile uint32_t g_usAvg = 0, g_usMax = 0, g_xruns = 0;
#if DIAG
float dgPk = 0; uint32_t dgN = 0, dgSobre = 0; unsigned long dgCtlMax = 0, dgOledMax = 0; volatile uint32_t dgPag = 0;
#endif

float PIW[1025], DRW[1025], PIT[6][1025];
static const char *PTIM_NOM[6] = { "PIANO", "FIELTRO", "ELECTRICO", "CAJA", "VIDRIO", "GRAVE" };
//                               tau rápida · tau lenta · brillo · caída del brillo · transposición · martillo
struct PTimb { float tf, ts, fc, kd; int8_t shift; float ham; };
static const PTimb PT[6] = {
  { 1.0f, 1.0f, 1.0f, 0.99993f,   0, 1.0f },    // PIANO
  { 0.8f, 0.6f, 0.45f, 0.99990f,  0, 0.5f },    // FIELTRO: piano apagado, íntimo
  { 1.3f, 1.1f, 0.7f, 0.99975f,   0, 0.2f },    // ELECTRICO: tipo Rhodes, cálido
  { 0.5f, 0.45f, 1.6f, 0.99995f, 12, 0.3f },    // CAJA: caja de música / celesta, una octava arriba
  { 1.0f, 1.4f, 1.3f, 0.99996f,   0, 0.1f },    // VIDRIO: armónicos impares, largo y cristalino
  { 1.4f, 1.5f, 0.55f, 0.99992f, -12, 0.8f } }; // GRAVE: una octava abajo, oscuro y largo

// ─── estado de la armonía (sólo el audio lo toca) ───
int esc = 0, ton = 2, grado = 0;
inline int midiEsc(int idx, int base) {                               // grado de escala (puede salirse de la octava) → nota MIDI
  int n = ESC_N[esc];
  int oc = idx >= 0 ? idx / n : -((-idx + n - 1) / n);
  int r = idx - oc * n;
  return base + 12 * oc + ESC_I[esc][r];
}
inline int tonBase() { return 60 + ((ton + 6) % 12) - 6; }            // la tónica más cercana al Do central
inline float noteHz(int m) { return 440.0f * powf(2.0f, ((float)m - 69.0f) * (1.0f / 12.0f)); }
inline bool esTonoAcorde(int idx) {                                   // ¿el grado cae en 1, 3 o 5 del acorde actual?
  int n = ESC_N[esc]; int d = ((idx - grado) % n + n) % n;
  return d == 0 || d == 2 % n || d == 4 % n;
}

// ==============================================================================================================================================
// VOCES
// ==============================================================================================================================================
#define NVP 12
struct VozP { const float *tab; uint32_t ph1, ph2, inc1, inc2; float a1, a2, d1, d2, lp, kc, kEnd, kd, ham, hp, gL, gR; bool on, fade; };
VozP vp[NVP];
#define NBELL 6
struct VozB { uint32_t ph, ph2, ph3, inc, inc2, inc3; float ar, d1, d2, d3, a1, a2, a3, gL, gR; bool on, fade; };
VozB vb[NBELL];
#define NMET 3
struct VozM { uint32_t ph[4]; uint32_t inc[4]; float a[4], d[4], gL, gR; bool on; };
VozM vm[NMET];
struct Pend { int32_t t; int8_t midi; float vel; uint8_t tipo; bool on; };   // tipo 0 piano · 1 campana
Pend pend[16];

// drón (dos juegos que se cruzan al cambiar el acorde), zumbido, golpe, vapor, barrido
uint32_t dph[2][6], dinc[2][6]; float dg[2] = { 1.0f, 0.0f }; int dAct = 0;
uint32_t hPh[3] = { 0, 0, 0 }; float hInc[3] = { 0, 0, 0 }, hIncT[3] = { 0, 0, 0 };
uint32_t tPh = 0; float tAmp = 0, tPit = 0, tPitF = 0, tDec = 0.9996f, tClk = 0, duck = 0;
float stAmp = 0, stAtk = 0, stDec = 0.99998f, stGL = 0.7f, stGR = 0.7f; bool stUp = false; SvfS stFL, stFR; Rampa stR;
float riProg = 0, riAmp = 0; bool riOn = false; SvfS riFL, riFR; Rampa riR;
uint32_t imPh = 0; float imAmp = 0, imNz = 0, imInc = 0;
SvfS inLs, inRs, drL, drR, coL, coR; Rampa inR, drRa, coRa;
float airLp = 0, hpL = 0, hpR = 0, hpK = 0.0035f, hpKT = 0.0035f;
float fcCo = 14000.0f, fcDr = 1200.0f, fcIn = 6000.0f;

// relojes (en muestras, por buffer)
float pAcc = 0, pStepLen = 24000, cAcc = 0, cLen = 192000, iAcc = 0, iHalf = 12000, aAcc = 0, aNext = 96000;
uint32_t pStep = 0, iStep = 0, mHits = 0;
int mIdx = 0, arpPos = 0, arpDir = 1; float metCool = 0;
float stOver = 0;
// ELECTRO: secuenciador de 16 pasos que muta; sus notas son grados relativos al acorde en curso (siempre en tono)
int8_t elOff[16]; uint8_t elGt[16]; bool elAc[16], elHit[16];
int elCnt = 1, elStepLen = 6000, elPos = 0, elGateSteps = 0, elMip = 2, elModoAct = -1; uint32_t elBar = 0, seenElGen = 0;
uint32_t elPh = 0, elPhS = 0, elPhM = 0; float elIncF = 0, elIncT = 0, elAmp = 0, elAmpT = 1, elFenv = 0, elFc = 400, elLvl = 0; bool elGate = false;
SvfS elF; Rampa elR;
// MICRO: entrada INMP441 (I2S1, 48 kHz) → ambiente / nube granular afinada al acorde / eco / sólo control; 0,68 s de memoria
#define MIC_WS 11
#define MIC_SCK 12
#define MIC_SD 13
#define GB_N 32768
int16_t *gBuf = 0; int gW = 0, gCnt = 1; float mHp = 0, micEnv = 0, infl = 0, micLvS = 0;
float mPrev = 0, micG = 8.0f, rawEnv = 0, nfl = 0.01f, micGate = 0; bool micOpen = false; volatile uint32_t g_clip = 0; volatile float g_micG = 0, g_gr = 0;   // AGC + compuerta del micro
struct Grano { float pos, rate, amp, gL, gR; int len, age; bool on; }; Grano gran[4];
SvfS mcF; Rampa mcR; float micIn[BUFFER_SAMPLES]; static i2s_chan_handle_t rx_chan; bool rxEn = false;
bool elKick[16], elHat[16], elOpn[16], elClk[16]; int elRitAct = -1;
uint32_t kPh = 0; float kAmp = 0, kPit = 50, kPitF = 150, kDec = 0.9998f, kDuck = 0, hAmp = 0, hDec = 0.995f; SvfS hF; Coef hK;
// INMERSIÓN: ancho, órbita del campo atmosférico, shimmer (octava arriba hacia la reverb), ensemble
float immS = 0, orbS = 0, ensS = 0, widS = 0, shiS = 0;
uint32_t orbPh = 0, chPh1 = 0, chPh2 = 0x55555555u; float shBuf[2048], shPh = 0, chL[1024], chR[1024], wBuf[1024]; int shW = 0, chW = 0, wI = 0;
bool vtOn = false; int vtType = 0; float vtT = 0, vtDur = 6, vtAmp = 0, vtAmpT = 0, vtTone = 0, vtFreq = 100, vtGL = 1, vtGR = 1, vtSend = 0.35f, vtG = 1, vtHpHz = 28, vtLpFc = 20000;
uint32_t vtPh = 0; SvfS vtFL, vtFR; Rampa vtR;
float rpAmp = 0, rpFc = 7000, rupDuck = 0; SvfS rpFL, rpFR; Rampa rpR;
uint32_t seenRup = 0, seenVia = 0, seenViaStop = 0, seenVapor = 0;

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
// ESTADO DEL AUDIO Y EVENTOS (todo corre en el núcleo 1; los eventos se resuelven al inicio de cada buffer: 2,7 ms de resolución sobra para ambiente)
// ==============================================================================================================================================
const float DC_R = 0.9986f;
float vPs = 0.6f, vIs = 0.4f, vAs = 0.5f, vMs = 0.8f, fxRs = 0.7f, fxG = 1.0f, fzS = 0.0f, revDecS = 0.7f, ecoAS = 0.3f, humG = 0.0f;
float notasE = 0.3f, densE = 0.3f, nubeE = 0.3f, tocaE = 0.4f, briE = 0.4f, oscE = 0.5f, tauK = 1.0f;
float lpOL = 0, lpOR = 0;
static i2s_chan_handle_t tx_chan;
inline float cl01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float rrange(float a, float b) { return a + (b - a) * rnd01(); }

volatile uint32_t g_heal = 0, g_bad = 0;
volatile bool g_lite = false;                                         // regulador de carga: si el render se acerca al límite, se aligeran shimmer, ensemble y polifonía
IRAM_ATTR void sanar();
void resetAudio() {
  memset(ia1, 0, sizeof(ia1)); memset(ia2, 0, sizeof(ia2)); memset(ia3, 0, sizeof(ia3)); memset(ia4, 0, sizeof(ia4));
  memset(lAp1, 0, sizeof(lAp1)); memset(lD1, 0, sizeof(lD1)); memset(lAp2, 0, sizeof(lAp2)); memset(lD2, 0, sizeof(lD2));
  memset(rAp1, 0, sizeof(rAp1)); memset(rD1, 0, sizeof(rD1)); memset(rAp2, 0, sizeof(rAp2)); memset(rD2, 0, sizeof(rD2));
  if (ecoL) { memset(ecoL, 0, ECO_N * 2); memset(ecoR, 0, ECO_N * 2); }
  lpL = lpR = bwIn = fbL = fbR = xAcc = pLo = pRo = pLn = pRn = 0; pFase = 0; ecoLpL = ecoLpR = eHpL = eHpR = 0; ecoW = 0;
  memset(shBuf, 0, sizeof(shBuf)); memset(chL, 0, sizeof(chL)); memset(chR, 0, sizeof(chR)); memset(wBuf, 0, sizeof(wBuf)); elF = SvfS{0, 0};
  memset(vp, 0, sizeof(vp)); memset(vb, 0, sizeof(vb)); memset(vm, 0, sizeof(vm)); memset(pend, 0, sizeof(pend));
  stFL = stFR = riFL = riFR = vtFL = vtFR = rpFL = rpFR = inLs = inRs = drL = drR = coL = coR = SvfS{0, 0};
}

IRAM_ATTR void sanar() {                                              // se llama si la mezcla deja de ser un número finito
  resetAudio();
  elAmp = 0; elGate = false; elFenv = 0; elIncF = elIncT; kAmp = 0; tAmp = 0; stAmp = 0; stUp = false; riAmp = 0; imAmp = 0; vtAmp = 0; rpAmp = 0; hAmp = 0;
  micEnv = 0; mHp = 0; micLvS = 0; immS = orbS = ensS = widS = shiS = 0; fzS = 0; vtOn = false; infl = 0;
  duck = 0; kDuck = 0; rupDuck = 0; lpOL = lpOR = 0; hpL = hpR = 0; ecoFb = 0.35f;
  for (int i = 0; i < 4; i++) gran[i].on = false;
  cmpEnv = 0; cmpGan = 1; cmpGT = 1; micG = 8.0f; rawEnv = 0; micGate = 0;
  g_heal = g_heal + 1;
}
// ─── disparadores de voz ───
void trigPiano(int midi, float vel) {
  const PTimb &pt = PT[((flagsA >> PTIM_SH) & 7) % 6];
  midi += pt.shift;
  int k = 0; float mn = 1e9f; const int nvp = g_lite ? 6 : NVP;
  for (int i = 0; i < nvp; i++) {
    if (!vp[i].on) { k = i; mn = -1; break; }
    float a = vp[i].a1 + vp[i].a2; if (vp[i].fade) a *= 0.05f;
    if (a < mn) { mn = a; k = i; }
  }
  VozP &q = vp[k];
  float hz = noteHz(midi);
  float reg = cl01(((float)midi - 40.0f) * (1.0f / 50.0f));          // 0 grave · 1 agudo
  float tf = (0.10f + 0.16f * (1.0f - reg)) * pt.tf;
  float ts = (0.8f + 1.6f * (1.0f - reg)) * tauK * pt.ts;
  float fcS = (700.0f + (900.0f + 5200.0f * tocaE) * (0.35f + 0.65f * vel)) * pt.fc;
  float fcE = (450.0f + 900.0f * tocaE) * pt.fc;
  q.inc1 = incF(hz); q.inc2 = incF(hz * 1.0012f); q.ph1 = 0; q.ph2 = 0x20000000u;
  q.a1 = 0.55f * vel; q.a2 = 0.45f * vel;
  q.d1 = expf(-1.0f / (tf * SR)); q.d2 = expf(-1.0f / (ts * SR));
  q.kc = 1.0f - expf(-6.2832f * fcS * INV_SR); q.kEnd = 1.0f - expf(-6.2832f * fcE * INV_SR); q.kd = pt.kd; q.tab = PIT[((flagsA >> PTIM_SH) & 7) % 6];
  q.ham = 0.10f * vel * (0.4f + tocaE) * pt.ham; q.hp = 0;
  float pan = ((float)midi - 62.0f) * (1.0f / 40.0f); if (pan > 1) pan = 1; if (pan < -1) pan = -1;
  pan *= 0.55f;
  q.gL = cosf((pan + 1.0f) * 0.7854f) * 0.95f; q.gR = sinf((pan + 1.0f) * 0.7854f) * 0.95f;
  q.lp = 0; q.fade = false; q.on = true;
}
void trigBell(int midi, float vel) {
  int k = 0; float mn = 1e9f; const int nbl = g_lite ? 3 : NBELL;
  for (int i = 0; i < nbl; i++) {
    if (!vb[i].on) { k = i; mn = -1; break; }
    float a = vb[i].a1 + vb[i].a2 + vb[i].a3; if (vb[i].fade) a *= 0.05f;
    if (a < mn) { mn = a; k = i; }
  }
  VozB &q = vb[k];
  float hz = noteHz(midi);
  float tau = rrange(1.8f, 3.6f);
  q.inc = incF(hz); q.inc2 = incF(hz * 2.76f); q.inc3 = incF(hz * 5.40f); q.ph = q.ph2 = q.ph3 = 0;
  q.a1 = vel; q.a2 = vel * 0.30f * (0.3f + briE); q.a3 = vel * 0.15f * (0.2f + briE);
  q.d1 = expf(-1.0f / (tau * SR)); q.d2 = expf(-1.0f / (tau * 0.45f * SR)); q.d3 = expf(-1.0f / (tau * 0.28f * SR));
  q.ar = 0; q.fade = false; q.on = true;
  float pan = rrange(-0.7f, 0.7f);
  q.gL = cosf((pan + 1.0f) * 0.7854f); q.gR = sinf((pan + 1.0f) * 0.7854f);
}
static const float MET_R[4] = { 1.0f, 2.003f, 3.007f, 5.011f };
void trigMetal(int midi, float vel) {
  int k = 0; float mn = 1e9f;
  for (int i = 0; i < NMET; i++) {
    if (!vm[i].on) { k = i; mn = -1; break; }
    float a = vm[i].a[0] + vm[i].a[1]; if (a < mn) { mn = a; k = i; }
  }
  VozM &q = vm[k];
  float hz = noteHz(midi), osc = oscE, len = rrange(0.7f, 1.3f);
  static const float W[4] = { 1.0f, 0.55f, 0.32f, 0.18f }, T[4] = { 0.90f, 0.60f, 0.40f, 0.25f };
  for (int j = 0; j < 4; j++) {
    q.inc[j] = incF(hz * MET_R[j]); q.ph[j] = 0;
    q.a[j] = vel * W[j] * (j == 0 ? 1.0f : (1.0f - 0.85f * osc * (float)j * 0.33f));
    q.d[j] = expf(-1.0f / (T[j] * len * SR));
  }
  float pan = rrange(-0.6f, 0.6f);
  q.gL = cosf((pan + 1.0f) * 0.7854f); q.gR = sinf((pan + 1.0f) * 0.7854f);
  q.on = true;
}
void trigThump(float vel) {
  float hz = noteHz(tonBase() - 24);
  tPit = hz; tPitF = hz * 2.3f; tAmp = 0.8f * vel; tPh = 0; tClk = 0.5f * vel;
  tDec = expf(-1.0f / (0.20f * SR));
}
void trigVapor() {
  return;                                                             // eliminado: soplido de ruido que crece (no se usa)
  if (stAmp > 0.01f || stUp) return;
  float fc = noteHz(tonBase() - 24 + 12 * (int)(xr() & 1));
  while (fc < 900.0f) fc *= 2.0f;
  stR.cur = coefDe(gDe(fc), 0.55f); stR.paso = Coef{0, 0, 0, 0};
  stAmp = 0.001f; stUp = true; stDec = expf(-1.0f / (rrange(1.0f, 1.8f) * SR));
  float pan = rrange(-0.8f, 0.8f); stGL = cosf((pan + 1.0f) * 0.7854f); stGR = sinf((pan + 1.0f) * 0.7854f);
}

void pendAdd(int ms, int midi, float vel, uint8_t tipo) {
  for (int i = 0; i < 16; i++) if (!pend[i].on) { pend[i].on = true; pend[i].t = (int32_t)(ms * 0.001f * SR); pend[i].midi = (int8_t)midi; pend[i].vel = vel; pend[i].tipo = tipo; return; }
}

// ─── armonía: acorde en curso y retoque del drón ───
void dronRetune() {
  int tipo = (flagsA >> TIPO_SH) & 3;
  static const int8_t OFS[4][3] = { { 0, 2, 4 }, { 0, 4, 8 }, { 0, 1, 4 }, { 0, 3, 4 } };
  int s = 1 - dAct;
  for (int j = 0; j < 3; j++) {
    float hz = noteHz(midiEsc(OFS[tipo][j], tonBase() - 24));
    dinc[s][2 * j] = incF(hz); dinc[s][2 * j + 1] = incF(hz * 1.0015f);
  }
  dAct = s;
}
void cambiarAcorde() {
  static const int8_t GR[8] = { 0, 0, 3, 4, 5, 2, 0, 3 };
  int n = ESC_N[esc], g = grado;
  for (int t = 0; t < 8; t++) { g = GR[xr() & 7] % n; if (g != grado) break; }
  grado = g; g_acorde = g;
  { float az = par[5][3]; if (!vtOn && az > 0.02f && rnd01() < az * az * 0.7f) iniciarViaje(xr() & 3); }
}

// ─── piano que se toca solo ───
int melNota(bool fuerte) {
  static const int8_t D[8] = { -2, -1, -1, 1, 1, 1, 2, 3 };
  int n = ESC_N[esc];
  int d = D[xr() & 7]; if ((xr() & 15) == 0) d = -d * 2;
  mIdx += d;
  int lo = -(n / 2) - 1, hi = n + n / 2 - 1;
  if (mIdx > hi) mIdx = hi - 1 - (int)(xr() & 1);
  if (mIdx < lo) mIdx = lo + 1 + (int)(xr() & 1);
  if (fuerte && !esTonoAcorde(mIdx) && (xr() & 3)) {
    for (int k = 1; k <= 2; k++) { if (esTonoAcorde(mIdx - k)) { mIdx -= k; break; } if (esTonoAcorde(mIdx + k)) { mIdx += k; break; } }
  }
  return midiEsc(mIdx, tonBase());
}
void acordePiano(float vel) {
  int n = ESC_N[esc];
  int idx[4] = { grado - n, grado, grado + 2, grado + 4 };
  int cnt = (xr() & 3) ? 3 : 4; int off = (cnt == 3) ? 1 : 0;
  for (int j = 0; j < cnt; j++) pendAdd(j * (int)rrange(28.0f, 60.0f), midiEsc(idx[j + off], tonBase()), vel * (1.0f - 0.10f * j), 0);
}
void pianoPaso() {
  int est = reqEstilo & 3; g_estilo = est;
  uint32_t s = pStep++;
  float vb0 = 0.28f + 0.50f * tocaE;
  float vel = vb0 * (0.7f + 0.5f * rnd01());
  int n = ESC_N[esc];
  float pr = 0.16f + 0.64f * notasE;
  g_pul = 1.0f;
  if (est == 0) {                                                    // MELODÍA: pasos por la escala, con silencios
    if (rnd01() < pr) pendAdd(0, melNota((s & 3) == 0), vel, 0);
  } else if (est == 1) {                                             // ARPEGIO: sube y baja por el acorde en dos octavas
    static const int8_t O[6] = { 0, 2, 4, 7, 9, 11 };
    if (rnd01() < 0.55f + 0.40f * notasE) {
      arpPos += arpDir; if (arpPos >= 5) { arpPos = 5; arpDir = -1; } if (arpPos <= 0) { arpPos = 0; arpDir = 1; }
      int off = O[arpPos]; if (n == 5) off = O[arpPos] * 5 / 7;
      pendAdd(0, midiEsc(grado - n + off, tonBase()), vel * 0.9f, 0);
    }
  } else if (est == 2) {                                             // ACORDES: rasgueos lentos
    int len = notasE < 0.33f ? 8 : (notasE < 0.66f ? 4 : 2);
    if ((s % len) == 0 && rnd01() < 0.85f) acordePiano(vel * 0.85f);
  } else {                                                           // MIXTO: acorde cada compás y melodía entre medio
    if ((s & 7) == 0) acordePiano(vel * 0.75f);
    else if (rnd01() < pr * 0.7f) pendAdd(0, melNota((s & 3) == 0), vel, 0);
  }
}

// ─── ELECTRO: patrón de 16 pasos (BAJO / ARPEGIO / SECUENCIA / PULSO) que muta compás a compás ───
static inline bool m_mutRit() { float m = par[6][3]; return rnd01() < m * m * 1.2f; }
IRAM_ATTR void elGenerar(int modo) {
  for (int i = 0; i < 16; i++) { elHit[i] = false; elOff[i] = 0; elGt[i] = 1; elAc[i] = false; }
  int n = ESC_N[esc];
  if (modo == 0) {                                                    // BAJO: pocas notas largas sobre la raíz del acorde
    static const int8_t P[4][5] = { { 0, 6, 10, -1, -1 }, { 0, 3, 8, 11, -1 }, { 0, 5, 7, 12, -1 }, { 0, 4, 8, 10, 14 } };
    const int8_t *p = P[xr() & 3];
    for (int k = 0; k < 5 && p[k] >= 0; k++) { int i = p[k]; elHit[i] = true; elOff[i] = (k > 0 && (xr() & 3) == 0) ? 4 : 0; elGt[i] = 2 + (xr() % 4); elAc[i] = (k == 0); }
  } else if (modo == 1) {                                             // ARPEGIO: tonos del acorde en semicorcheas con silencios
    static const int8_t O[4] = { 0, 2, 4, 7 };
    int a = xr() & 3;
    for (int i = 0; i < 16; i++) if (rnd01() > 0.18f) { elHit[i] = true; elOff[i] = O[(i + a) & 3]; elAc[i] = ((i & 3) == 0); }
  } else if (modo == 2) {                                             // SECUENCIA: línea que camina por la escala
    static const int8_t D[8] = { -2, -1, -1, 1, 1, 1, 2, 3 };
    int idx = 0;
    for (int i = 0; i < 16; i++) if ((i & 3) == 0 || rnd01() < 0.55f) {
      idx += D[xr() & 7]; if (idx > n + 2) idx = n; if (idx < -3) idx = -2;
      elHit[i] = true; elOff[i] = idx; elGt[i] = 1 + (xr() & 1); elAc[i] = ((i & 3) == 0);
    }
  } else {                                                            // PULSO: la raíz machacada, con octavas y acentos
    for (int i = 0; i < 16; i++) if ((i & 1) == 0 || rnd01() < 0.15f) { elHit[i] = true; elOff[i] = ((xr() & 7) == 0) ? n : 0; elAc[i] = ((i & 3) == 0); }
  }
  elHit[0] = true;
}
IRAM_ATTR void elMutar(float m) {                                     // m = 0..1 (POT4²): cuántos pasos cambian en este compás
  float c = m * 5.0f; int cnt = (int)c; if (rnd01() < c - (float)cnt) cnt++;
  int n = ESC_N[esc];
  for (int j = 0; j < cnt; j++) {
    int i = xr() & 15, r = xr() & 3;
    if (r == 0) { if (i != 0) elHit[i] = !elHit[i]; }
    else if (r == 1) { int o = elOff[i] + ((xr() & 1) ? 1 : -1); if (o > n + 2) o = n + 2; if (o < -4) o = -4; elOff[i] = (int8_t)o; elHit[i] = true; }
    else if (r == 2) elAc[i] = !elAc[i];
    else elGt[i] = 1 + (xr() % 4);
  }
}
IRAM_ATTR void elRitGenerar(int r) {                                  // ritmo industrial de la capa: bombo afinado a la tónica, hats en banda y clanks del acorde
  for (int i = 0; i < 16; i++) { elKick[i] = elHat[i] = elOpn[i] = elClk[i] = false; }
  if (r == 1) {                                                       // PULSO: bombo a negras, hat abierto a contratiempo
    for (int i = 0; i < 16; i += 4) elKick[i] = true;
    for (int i = 2; i < 16; i += 4) { elHat[i] = true; elOpn[i] = true; }
  } else if (r == 2) {                                                // ROTO: bombo sincopado, clanks sueltos
    static const int8_t K[4][5] = { { 0, 3, 6, 10, 12 }, { 0, 4, 7, 10, 14 }, { 0, 3, 8, 11, 14 }, { 0, 5, 8, 10, 13 } };
    const int8_t *k = K[xr() & 3];
    for (int j = 0; j < 5; j++) elKick[k[j]] = true;
    for (int i = 0; i < 16; i++) if ((i & 1) && rnd01() < 0.35f) elHat[i] = true;
    elClk[7] = true; if (xr() & 1) elClk[15] = true;
  } else if (r == 3) {                                                // DENSO: bombo a negras, hats a semicorcheas, clanks aleatorios
    for (int i = 0; i < 16; i += 4) elKick[i] = true;
    if (xr() & 1) elKick[14] = true;
    for (int i = 0; i < 16; i++) { elHat[i] = true; elOpn[i] = ((i & 3) == 2); }
    for (int j = 0; j < 2; j++) elClk[xr() & 15] = true;
  }
}
IRAM_ATTR void elPaso() {
  int i = elPos; elPos = (elPos + 1) & 15;
  if (i == 0) { elBar++; float m = cl01(par[6][3] + 0.6f * infl); m *= m; if (m > 0.012f) elMutar(m); }
  { int rit = (flagsA >> ERIT_SH) & 3;
    if (rit > 0) {
      if (i == 0 && m_mutRit()) { int j = xr() & 15, q = xr() & 3; if (q == 0 && j != 0) elKick[j] = !elKick[j]; else if (q == 1) elHat[j] = !elHat[j]; else if (q == 2) elClk[j] = !elClk[j]; else elOpn[j] = !elOpn[j]; }
      if (elKick[i]) { float f0 = noteHz(tonBase() - 24); kPit = f0; kPitF = f0 * 3.2f; kPh = 0; kAmp = 0.95f; kDec = expf(-1.0f / (0.26f * SR)); kDuck = 1.0f; }
      if (elHat[i]) { hAmp = elOpn[i] ? 0.55f : 0.40f; hDec = expf(-1.0f / ((elOpn[i] ? 0.16f : 0.035f) * SR)); }
      if (elClk[i]) { static const int8_t C[3] = { 0, 2, 4 }; trigMetal(midiEsc(grado + C[xr() % 3], tonBase() - 12), 0.55f); }
    }
  }
  if (elGateSteps > 0 && --elGateSteps == 0) elGate = false;
  if (!elHit[i]) return;
  int modo = (flagsA >> EMOD_SH) & 3;
  int base = (modo == 0 || modo == 3) ? tonBase() - 24 : tonBase() - 12;
  float hz = noteHz(midiEsc(grado + elOff[i], base));
  elIncT = hz * INC_K; elMip = mipDe(hz);
  if (!elGate && elAmp < 0.01f) elIncF = elIncT;                      // nota nueva sin glisando
  elGate = true; elGateSteps = elGt[i];
  elFenv = elAc[i] ? 1.4f : 0.8f; elAmpT = elAc[i] ? 1.0f : 0.72f;
  g_pulE = 1.0f;
}

// ─── RUPTURA industrial (rompe la calma) y VIAJES (transiciones) ───
void ruptura(float f) {
  trigThump(1.6f * f); tDec = expf(-1.0f / (0.55f * SR));
  static const int8_t C[3] = { 0, 2, 4 };
  for (int j = 0; j < 3; j++) trigMetal(midiEsc(grado + C[j], tonBase() - 12 + 12 * (j & 1)), 0.9f * f);
  rpAmp = 0.9f * f; rpFc = 7000.0f; rupDuck = 0.4f + 0.6f * f;
  g_pulI = 1.0f;
}
void iniciarViaje(int tipo) {
  return;
  vtOn = true; vtType = tipo & 3; vtT = 0; vtDur = 2.0f + 12.0f * par[5][1]; vtPh = 0;
  vtG = 0.4f + 1.0f * par[5][2];
  if (vtType == 1) ruptura(0.4f + 0.8f * par[5][2]);               // la BAJADA arranca con el golpe
}
void actualizarViaje() {
  vtOn = false; reqBarrido = false; return;                            // eliminados: viajes, rupturas con barrido y riser (ruido que crece de menor a mayor)
  if (reqVaporN != seenVapor) { seenVapor = reqVaporN; trigVapor(); }
  if (reqRupN != seenRup) { seenRup = reqRupN; ruptura(0.4f + 0.8f * par[5][2]); }
  if (reqViajeStopN != seenViaStop) { seenViaStop = reqViajeStopN; vtOn = false; }
  if (reqViajeN != seenVia) { seenVia = reqViajeN; iniciarViaje(reqViajeTipo); }
  rupDuck *= 0.9982f;
  rpFc *= 0.992f; if (rpFc < 250.0f) rpFc = 250.0f;
  vtHpHz = 28.0f; vtLpFc = 20000.0f;
  float tgt = 0, pan = 0.5f, send = 0.35f; vtTone = 0;
  if (vtOn) {
    vtT += (float)BUFFER_SAMPLES / (SR * vtDur);
    float u = vtT; if (u > 1.0f) u = 1.0f;
    float rootHz = noteHz(tonBase() - 24);
    float fc = 1000, q = 0.55f;
    if (vtType == 0) {                                              // SUBIDA: ruido que sube + tono que sube + pasa-altos del master; termina en ruptura
      fc = 200.0f * powf(35.0f, u); tgt = u * u * 0.6f; vtFreq = rootHz * 2.0f * powf(4.0f, u); vtTone = u * 0.30f; vtHpHz = 28.0f + 1400.0f * u * u; send = 0.5f;
    } else if (vtType == 1) {                                       // BAJADA: cae el ruido, cae el tono, se cierra el master
      float v = 1.0f - u; fc = 6000.0f * powf(0.04f, u); tgt = v * v * 0.5f; vtFreq = rootHz * 8.0f * powf(0.125f, u); vtTone = v * 0.28f; vtLpFc = 12000.0f * powf(0.06f, u); send = 0.5f;
    } else if (vtType == 2) {                                       // REVERSA: oleaje que crece y se corta, con mucha cola de reverb
      float e = u * u * u; fc = 900.0f + 2500.0f * u; tgt = e * 0.7f; vtFreq = rootHz * 4.0f; vtTone = e * 0.18f; send = 1.0f;
      if (u > 0.97f) tgt *= (1.0f - u) * 33.0f;
    } else {                                                        // TURBINA: barrido resonante de ida y vuelta que cruza de izquierda a derecha
      float tr = u < 0.5f ? 2.0f * u : 2.0f - 2.0f * u; fc = 300.0f * powf(18.0f, tr); q = 0.35f;
      tgt = sinf(3.1416f * u); if (tgt < 0.0f) tgt = 0.0f; tgt = tgt * sqrtf(tgt) * 0.6f;      // sinf(π) da −8e-8: sqrtf de un negativo = NaN, y el NaN envenenaba toda la mezcla pan = u; send = 0.4f;
    }
    Coef c = coefDe(gDe(fc), q); rampaHacia(vtR, c, 1.0f / BUFFER_SAMPLES);
    vtGL = 1.414f * sqrtf(1.0f - pan); vtGR = 1.414f * sqrtf(pan);
    vtSend = send;
    if (vtT >= 1.0f) {                                              // fin del viaje
      vtOn = false;
      if (vtType == 0) ruptura(0.5f + 0.9f * par[5][2]); else if (vtType == 2) trigThump(0.6f);
    }
  }
  if (!vtOn) vtR.paso = Coef{0, 0, 0, 0};                             // sin viaje, la rampa NO debe seguir avanzando (los coeficientes se disparaban)
  { Coef c2 = coefDe(gDe(rpFc), 0.6f); rampaHacia(rpR, c2, 1.0f / BUFFER_SAMPLES); }
  vtAmpT = tgt * vtG;
  vtAmp += (vtAmpT - vtAmp) * 0.3f;
}

// ─── industrial: pulso, metales, vapor (cada medio tiempo) ───
void industrialPaso() {
  uint32_t h = iStep++;
  uint32_t fl = flagsA;
  bool tiempo = ((h & 1) == 0);
  if (tiempo) g_pulI = 1.0f;
  if ((fl & FL_PULSO) && vIs > 0.002f) {
    if (tiempo) trigThump(1.0f);
    else if (rnd01() < densE * 0.30f) trigThump(0.45f);
  }
  if (fl & FL_METAL) {
    if (rnd01() < 0.06f + 0.34f * densE && metCool <= 0.0f) {
      int n = ESC_N[esc]; static const int8_t C[3] = { 0, 2, 4 };
      trigMetal(midiEsc(grado + C[xr() % 3], tonBase() - 12 + 12 * (int)(xr() & 1)), rrange(0.35f, 0.9f));
      metCool = 0.9f;
    }
  }
}
// ─── ambiente: nube de campanas con la escala y el acorde ───
void ambientePaso() {
  g_pulA = 1.0f;
  if (!(flagsA & FL_NUBE) || nubeE < 0.02f) return;
  int n = ESC_N[esc];
  int cantidad = (xr() % 3 == 0) ? 3 : 1;
  int ult = -1000;
  for (int j = 0; j < cantidad; j++) {
    int idx = n + (int)(xr() % (uint32_t)(n + n / 2));
    if ((xr() & 3) != 0) { for (int k = 0; k < 3 && !esTonoAcorde(idx); k++) idx++; }
    if (idx == ult) idx++;
    ult = idx;
    int m = midiEsc(idx, tonBase());
    while (m > 86) m -= 12;
    pendAdd(j * (int)rrange(260.0f, 700.0f), m, (0.25f + 0.55f * nubeE) * rrange(0.6f, 1.0f), 1);
  }
}

// ==============================================================================================================================================
// RENDER: un buffer de 128 muestras
// ==============================================================================================================================================
float acPian = 0, acInd = 0, acAmb = 0;
#if DIAG
uint32_t ccS[10] = {0,0,0,0,0,0,0,0,0,0}; uint32_t ccT0; uint32_t ccBuf = 0;
#define MK(n) { uint32_t _t = xthal_get_ccount(); ccS[n] += _t - ccT0; ccT0 = _t; }
#else
#define MK(n)
#endif
IRAM_ATTR void renderCore(int32_t *buffer) {
  esc = reqEsc % NESC; ton = reqTon % 12; g_esc = esc; g_ton = ton;
  if (grado >= ESC_N[esc]) grado %= ESC_N[esc];
  const uint32_t fl = flagsA; const uint8_t run = runMask;
  const float tmul = 0.5f + 1.5f * par[4][1];
  const float ofs = (par[4][3] - 0.5f) * 0.8f;
  notasE = cl01(par[0][3] + ofs + 0.25f * infl); densE = cl01(par[1][2] + ofs); nubeE = cl01(par[2][3] + ofs * 0.8f);
  tocaE = par[0][2]; briE = par[2][2]; oscE = par[1][3];
  const float revP = cl01(par[3][2] + ofs * 0.4f), ecoP = par[3][3];
  tauK = 1.0f / (1.0f + 1.6f * notasE);
  const bool cong = (fl & FL_CONG) != 0, byp = (fl & FL_BYP) != 0;
  const int colSel = (fl >> COL_SH) & 3;
  // suavizado por buffer
  { float v;
    v = par[0][0]; vPs += (v * v - vPs) * 0.08f;
    v = par[1][0]; vIs += (v * v - vIs) * 0.08f;
    v = par[2][0]; vAs += (v * v - vAs) * 0.08f;
    v = par[4][0]; vMs += (1.3f * v * v - vMs) * 0.08f;
    fxRs += (par[3][0] * 1.4f - fxRs) * 0.08f;
    fxG += ((byp ? 0.0f : 1.0f) - fxG) * 0.12f;
    fzS += ((cong ? 1.0f : 0.0f) - fzS) * 0.025f;
    float dT = 0.50f + 0.40f * revP; revDecS += (dT - revDecS) * 0.05f;
    ecoAS += (ecoP - ecoAS) * 0.08f;
    humG += (((run & 2) ? 1.0f : 0.0f) - humG) * 0.01f;
  }
  // eventos
  const float bpmP = (30.0f + 90.0f * par[0][1]) * tmul;
  const float beatP = SR * 60.0f / bpmP;
  if (run & 1) { pAcc += BUFFER_SAMPLES; if (pAcc >= pStepLen) { pAcc -= pStepLen; pianoPaso(); pStepLen = beatP / (notasE > 0.7f ? 2.0f : 1.0f); } }
  else pAcc = pStepLen;
  cAcc += BUFFER_SAMPLES; if (cAcc >= cLen) { cAcc = 0; cLen = beatP * 16.0f; cambiarAcorde(); }
  const float bpmI = (30.0f + 60.0f * par[1][1]) * tmul;
  if (run & 2) { iAcc += BUFFER_SAMPLES; if (iAcc >= iHalf) { iAcc -= iHalf; industrialPaso(); iHalf = SR * 60.0f / (bpmI * 2.0f); } }
  else iAcc = iHalf;
  if (metCool > 0.0f) metCool -= (float)BUFFER_SAMPLES * INV_SR;
  if (run & 4) {
    aAcc += BUFFER_SAMPLES;
    if (aAcc >= aNext) {
      aAcc = 0; ambientePaso();
      float mean = (9.0f - 7.5f * par[2][1]) / (tmul * (0.4f + 1.6f * nubeE));
      float u = rnd01(); if (u < 0.02f) u = 0.02f;
      aNext = -logf(u) * mean * SR;
    }
  }
  for (int i = 0; i < 16; i++) if (pend[i].on) {
    pend[i].t -= BUFFER_SAMPLES;
    if (pend[i].t <= 0) { pend[i].on = false; if (pend[i].tipo == 0) trigPiano(pend[i].midi, pend[i].vel); else trigBell(pend[i].midi, pend[i].vel); }
  }
  actualizarViaje(); g_viaje = vtOn;
  if (reqVaporN != seenVapor) {                                       // GOLPE: tres metales del acorde + golpe grave afinado, sin ruido
    seenVapor = reqVaporN; static const int8_t C[3] = { 0, 2, 4 };
    for (int j = 0; j < 3; j++) trigMetal(midiEsc(grado + C[j], tonBase() - 12 + 12 * (j & 1)), 0.8f);
    trigThump(1.2f); g_pulI = 1.0f;
  }
  { static int dEsc = -1, dTon = -1, dTipo = -1; int tp = (fl >> TIPO_SH) & 3;
    if (dEsc != esc || dTon != ton || dTipo != tp) { if (dEsc >= 0) dronRetune(); dEsc = esc; dTon = ton; dTipo = tp; } }
  // ELECTRO y INMERSIÓN: parámetros por buffer
  const int elTim = (fl >> ETIM_SH) & 3, elModo = (fl >> EMOD_SH) & 3;
  if (elModo != elModoAct) { elModoAct = elModo; elGenerar(elModo); }
  const int elRit = (fl >> ERIT_SH) & 3;
  if (elRit != elRitAct) { elRitAct = elRit; elRitGenerar(elRit); }
  if (reqElGenN != seenElGen) { seenElGen = reqElGenN; elGenerar(elModo); elRitGenerar(elRit); }
  { float bpmE = (60.0f + 90.0f * par[6][1]) * tmul * (1.0f + 0.12f * infl); elStepLen = (int)(SR * 60.0f / (bpmE * 4.0f));
    float v = par[6][0]; elLvl += (((run & 8) ? v * v : 0.0f) * 0.45f - elLvl) * 0.08f;
    float fb = 100.0f + 1800.0f * par[6][2] * par[6][2];
    elFc = fb * (1.0f + 3.0f * elFenv) * (1.0f + 2.2f * infl); if (elFc > 4500.0f) elFc = 4500.0f;
    Coef c = coefDe(gDe(elFc), 0.55f); rampaHacia(elR, c, 1.0f / BUFFER_SAMPLES);
    if (!(run & 8)) { elGate = false; elGateSteps = 0; elCnt = 1; elPos = 0; }
    float im = (run & 16) ? par[7][0] * par[7][0] * 1.2f : 0.0f; immS += (im - immS) * 0.08f;
    float ic = cl01(immS);
    orbS += (((fl & FL_ORB) ? 0.7f * ic : 0.0f) - orbS) * 0.05f;
    ensS += (((fl & FL_ENS) ? ic : 0.0f) - ensS) * 0.05f;
    widS += (par[7][2] * ic - widS) * 0.08f;
    shiS += (((fl & FL_SHI) ? par[7][3] * ic : 0.0f) - shiS) * 0.08f; }
  const bool lite = g_lite;
  const bool micAct = (run & 32) != 0;
  const int mUso = (fl >> MUSO_SH) & 3; const bool mCong = (fl & MCON) != 0;
  if (micAct) {                                                       // AUTO-AJUSTE del micro: ganancia que sube despacio y baja rápido, compuerta sobre el piso de ruido
    float tg = 0.06f / (rawEnv + 1e-5f); if (tg > 40.0f) tg = 40.0f; if (tg < 1.0f) tg = 1.0f;
    if (tg < micG) micG += (tg - micG) * 0.35f; else micG += (tg - micG) * 0.004f;
    if (rawEnv < nfl) nfl = rawEnv; else nfl += (rawEnv - nfl) * 0.0003f;
    micOpen = rawEnv > nfl * 2.5f + 0.0015f; g_micG = micG;
  }
  { float tI = (micAct && (fl & MINF)) ? cl01((micEnv - 0.03f) * 5.0f) : 0.0f; infl += (tI - infl) * 0.10f; g_micEnv = micEnv;
    float v = par[8][0]; micLvS += ((micAct ? v * v * 1.5f : 0.0f) - micLvS) * 0.08f;
    float fcm = 250.0f + 7000.0f * par[8][2] * par[8][2]; Coef c = coefDe(gDe(fcm), 0.8f); rampaHacia(mcR, c, 1.0f / BUFFER_SAMPLES); }
  const float micWet = 0.3f + 0.7f * par[8][3];
  const uint32_t orbInc = incF(0.03f + 0.5f * par[7][1] * par[7][1]);
  const float elLv = elLvl;
  // tónica del zumbido y bandas de sintonía
  { float hz = noteHz(tonBase() - 24);
    hIncT[0] = (float)incF(hz); hIncT[1] = (float)incF(hz * 2.0f); hIncT[2] = (float)incF(hz * 3.0f);
    for (int k = 0; k < 3; k++) { if (hInc[k] == 0) hInc[k] = hIncT[k]; hInc[k] += (hIncT[k] - hInc[k]) * 0.005f; } }
  // barrido (riser + impacto)
  if (reqBarrido) { riOn = true; if (riProg < 1.0f) riProg += (float)BUFFER_SAMPLES / (5.0f * SR); }
  else if (riOn) {
    riOn = false;
    if (riProg > 0.12f) { imAmp = 0.35f + 0.65f * riProg; imNz = 0.5f * riProg; imPh = 0; imInc = (float)incF(noteHz(tonBase() - 24)); }
  } else riProg *= 0.97f;
  { float fc = 250.0f * powf(24.0f, riProg);
    Coef t = coefDe(gDe(fc), 0.9f); rampaHacia(riR, t, 1.0f / BUFFER_SAMPLES); }
  // filtros por buffer: oscuridad industrial, brillo del drón, color del master
  { float fi = 450.0f + 7000.0f * (1.0f - par[1][3]) * (1.0f - par[1][3]);
    fcIn += (fi - fcIn) * 0.2f; Coef t = coefDe(gDe(fcIn), 0.9f); rampaHacia(inR, t, 1.0f / BUFFER_SAMPLES);
    float fd = 220.0f + 3600.0f * par[2][2] * par[2][2];
    fcDr += (fd - fcDr) * 0.2f; t = coefDe(gDe(fcDr), 0.8f); rampaHacia(drRa, t, 1.0f / BUFFER_SAMPLES);
    static const float COL_FC[4] = { 14500.0f, 4800.0f, 1700.0f, 12000.0f };
    float lpT = COL_FC[colSel]; if (vtLpFc < lpT) lpT = vtLpFc;
    fcCo += (lpT - fcCo) * 0.08f; t = coefDe(gDe(fcCo), 0.8f); rampaHacia(coRa, t, 1.0f / BUFFER_SAMPLES);
    hpKT = (colSel == 3) ? 0.034f : 0.0036f;
    { float kv = 6.2832f * vtHpHz * INV_SR; if (kv > 0.4f) kv = 0.4f; if (kv > hpKT) hpKT = kv; } hpK += (hpKT - hpK) * 0.08f; }
  // ecos: tiempo de eco = corchea con puntillo del BPM del eco
  { float ebpm = 66.0f + 84.0f * par[3][1];
    int nuevo = (int)(((fl & FL_ECO2) ? 0.5f : 0.75f) * SR * 60.0f / ebpm); if (nuevo > ECO_N - 8) nuevo = ECO_N - 8;
    if (ecoX <= 0.0f && abs(nuevo - ecoDA) > 24) { ecoDB = ecoDA; ecoDA = nuevo; ecoX = 1.0f; } }
  const float revEnvio = (0.12f + 0.88f * revP) * 2.2f * (1.0f - fzS) * fxG * (1.0f + 0.8f * infl);
  const float revDecay = revDecS + (0.985f - revDecS) * fzS;
  const float revRet = (0.20f + 0.60f * revP) * fxRs;
  const float ecoFbT = 0.20f + 0.45f * ecoAS;
  const float dGate = (run & 4) ? 1.0f : 0.0f;
  const float gP = vPs, gI = vIs, gA = vAs;
  const float stepD = 1.0f / (0.25f * SR);
  float acP = 0, acI = 0, acA = 0, acE = 0;

#if DIAG
  ccT0 = xthal_get_ccount(); MK(9)
  ccBuf++;
#endif
  for (int i = 0; i < BUFFER_SAMPLES; i++) {
    rampaAvanza(inR); rampaAvanza(drRa); rampaAvanza(coRa); rampaAvanza(riR); rampaAvanza(vtR); rampaAvanza(rpR); rampaAvanza(elR); rampaAvanza(mcR);
    // — piano —
    float pl = 0, pr = 0;
    for (int v = 0; v < NVP; v++) {
      VozP &q = vp[v]; if (!q.on) continue;
      q.ph1 += q.inc1;
      float s = tabPh(q.tab, q.ph1) * 1.3f;
      float a = q.a1 + q.a2; q.a1 *= q.d1; q.a2 *= q.d2;
      q.kc = q.kEnd + (q.kc - q.kEnd) * q.kd;
      q.lp += q.kc * (s * a - q.lp);
      float o = q.lp;
      if (q.ham > 0.0006f) { q.hp += 0.25f * (ruido() * q.ham - q.hp); o += q.hp; q.ham *= 0.9985f; }
      pl += o * q.gL; pr += o * q.gR;
      if (!q.fade) { if (a < (g_lite ? 0.03f : 0.015f)) { q.fade = true; q.d1 = q.d2 = 0.9857f; } } else if (a < 0.00005f) q.on = false;
    }
    MK(0)
    // — campanas —
    float bl = 0, br = 0;
    for (int v = 0; v < NBELL; v++) {
      VozB &q = vb[v]; if (!q.on) continue;
      q.ph += q.inc; q.ph2 += q.inc2; q.ph3 += q.inc3;
      float s = sinPh(q.ph) * q.a1 + sinPh(q.ph2) * q.a2 + sinPh(q.ph3) * q.a3;
      if (q.ar < 1.0f) { q.ar += 0.0042f; if (q.ar > 1.0f) q.ar = 1.0f; s *= q.ar; }
      float a = q.a1 + q.a2 + q.a3; q.a1 *= q.d1; q.a2 *= q.d2; q.a3 *= q.d3;
      bl += s * q.gL; br += s * q.gR;
      if (!q.fade) { if (a < 0.0025f) { q.fade = true; q.d1 = q.d2 = q.d3 = 0.9857f; } } else if (a < 0.00005f) q.on = false;
    }
    bl *= 0.50f; br *= 0.50f;
    MK(1)
    // — drón (dos juegos que se cruzan) —
    float dl = 0, dr = 0;
    { float t0 = (dAct == 0 ? 1.0f : 0.0f) * dGate, t1 = (dAct == 1 ? 1.0f : 0.0f) * dGate;
      if ((fl & FL_DRON) == 0) { t0 = t1 = 0; }
      float d0 = t0 - dg[0]; dg[0] += d0 > stepD ? stepD : (d0 < -stepD ? -stepD : d0);
      float d1 = t1 - dg[1]; dg[1] += d1 > stepD ? stepD : (d1 < -stepD ? -stepD : d1);
      for (int sdx = 0; sdx < 2; sdx++) {
        if (dg[sdx] < 0.0003f) continue;
        float l = 0, r = 0;
        for (int j = 0; j < 3; j++) {
          dph[sdx][2 * j] += dinc[sdx][2 * j]; dph[sdx][2 * j + 1] += dinc[sdx][2 * j + 1];
          l += tabPh(DRW, dph[sdx][2 * j]); r += tabPh(DRW, dph[sdx][2 * j + 1]);
        }
        dl += l * dg[sdx]; dr += r * dg[sdx];
      }
      dl = svfLP(drL, drRa.cur, dl + 1e-18f) * 0.30f; dr = svfLP(drR, drRa.cur, dr + 1e-18f) * 0.30f; }
    MK(2)
    // — industrial —
    hPh[0] += (uint32_t)hInc[0]; hPh[1] += (uint32_t)hInc[1]; hPh[2] += (uint32_t)hInc[2];
    float hum = (sinPh(hPh[0]) * 0.50f + sinPh(hPh[1]) * 0.24f + sinPh(hPh[2]) * 0.09f) * humG * 0.50f;
    float th = 0;
    if (tAmp > 0.0008f) {
      tPitF = tPit + (tPitF - tPit) * 0.9990f; tPh += incF(tPitF);
      th = sat3(sinPh(tPh) * 1.5f) * tAmp + ruido() * tClk * 0.35f;
      tAmp *= tDec; tClk *= 0.993f; th *= 1.0f;
    } else tAmp = 0;
    float ml = 0, mr = 0;
    for (int v = 0; v < NMET; v++) {
      VozM &q = vm[v]; if (!q.on) continue;
      float s = 0, sum = 0;
      for (int j = 0; j < 4; j++) { q.ph[j] += q.inc[j]; s += sinPh(q.ph[j]) * q.a[j]; sum += q.a[j]; q.a[j] *= q.d[j]; }
      ml += s * q.gL; mr += s * q.gR;
      if (sum < 0.0015f) { q.on = false; }
    }
    ml *= 0.35f; mr *= 0.35f;
    float vl = 0, vr = 0;
    if (stAmp > 0.0015f) {
      float nz = ruido();
      float e = stAmp;
      float o = svfBP(stFL, stR.cur, nz) * e * 1.5f;
      if (stUp) { stAmp += 1.0f / (0.35f * SR); if (stAmp >= 1.0f) { stAmp = 1.0f; stUp = false; } } else stAmp *= stDec;
      vl = o * stGL; vr = o * stGR; (void)stFR;
    } else { stAmp = 0; stUp = false; }
    float ri = 0, rl2 = 0, rr2 = 0;
    if (riOn || riAmp > 0.0008f) {
      float tgt = riOn ? riProg * riProg * 0.5f : 0.0f;
      riAmp += (tgt - riAmp) * (riOn ? 0.0006f : 0.0030f);
      rl2 = svfBP(riFL, riR.cur, ruido()) * riAmp; rr2 = svfBP(riFR, riR.cur, ruido()) * riAmp;
    }
    float im = 0;
    if (imAmp > 0.0008f) {
      imPh += (uint32_t)imInc;
      im = sat3(sinPh(imPh) * 1.3f) * imAmp * 0.5f + ruido() * imNz * 0.18f;
      imAmp *= 0.999977f; imNz *= 0.99997f;
    }
    float micL = 0, micR = 0, micSR = 0, micSE = 0;
    if (micAct) {
      float xr0 = micIn[i];
      if (fabsf(xr0 - mPrev) > 0.5f) xr0 = mPrev;                    // antipicos: un salto brusco de la entrada se retiene (no pasa al AGC ni a la mezcla)
      mPrev = xr0; float a0 = fabsf(xr0);
      rawEnv += (a0 - rawEnv) * (a0 > rawEnv ? 0.003f : 0.00008f);
      if (a0 > 0.95f) g_clip = g_clip + 1;
      micGate += ((micOpen ? 1.0f : 0.0f) - micGate) * (micOpen ? 0.01f : 0.0008f);
      float x = xr0 * micG * micGate;
      mHp += 0.015f * (x - mHp); x -= mHp;
      float ax = fabsf(x); micEnv += (ax - micEnv) * (ax > micEnv ? 0.01f : 0.0004f);
      x = sat3(x);
      if (!mCong) { gBuf[gW] = (int16_t)(x * 20000.0f); gW = (gW + 1) & (GB_N - 1); }
      float xm = svfLP(mcF, mcR.cur, x + 1e-18f);
      if (mUso == 0) { micL = micR = xm; micSR = xm * 0.9f; micSE = xm * 0.15f; }
      else if (mUso == 2) { micL = micR = xm * 0.25f; micSR = xm * 0.5f; micSE = xm; }
      else if (mUso == 1) {
        if (--gCnt <= 0) {
          gCnt = (int)(SR * (0.03f + 0.40f * (1.0f - par[8][1])) * rrange(0.6f, 1.4f));
          for (int k = 0; k < (lite ? 2 : 4); k++) if (!gran[k].on) {
            static const int8_t Q[8] = { 0, 2, 4, 7, -5, 9, 11, 14 };      // grados del acorde en dos octavas (en escala y en acorde)
            float spread = par[8][3];                                       // POT4 en NUBE = abanico de alturas: 0 unísono … 1 dos octavas
            int nq = 1 + (int)(spread * 7.0f + 0.5f);
            int semi = midiEsc(grado + Q[xr() % (uint32_t)nq], 60) - midiEsc(grado, 60);
            Grano &g = gran[k]; g.rate = powf(2.0f, (float)semi * (1.0f / 12.0f)) * rrange(0.997f, 1.003f);
            g.pos = (float)((gW - 4000 - (int)(xr() % 24000)) & (GB_N - 1)); g.len = 2400 + (int)(xr() % 7200); g.age = 0;
            g.amp = rrange(0.5f, 1.0f); float p = rrange(0.1f, 0.9f); g.gL = 1.414f * sqrtf(1.0f - p); g.gR = 1.414f * sqrtf(p); g.on = true; break;
          }
        }
        float gl = 0, gr = 0;
        for (int k = 0; k < 4; k++) {
          Grano &g = gran[k]; if (!g.on) continue;
          int ip = (int)g.pos; float fr = g.pos - (float)ip;
          float a = (float)gBuf[ip & (GB_N - 1)], b2 = (float)gBuf[(ip + 1) & (GB_N - 1)];
          float u = (float)g.age / (float)g.len; float w = 1.0f - fabsf(2.0f * u - 1.0f);
          float v = (a + (b2 - a) * fr) * (1.0f / 20000.0f) * w * g.amp;
          gl += v * g.gL; gr += v * g.gR;
          g.pos += g.rate; if (g.pos >= (float)GB_N) g.pos -= (float)GB_N;
          if (++g.age >= g.len) g.on = false;
        }
        micL = gl * 0.9f; micR = gr * 0.9f; micSR = (gl + gr) * 0.4f; micSE = (gl + gr) * 0.2f;
      }
    }
    float elO = 0, elD = 0;
    if (run & 8) { if (--elCnt <= 0) { elCnt += elStepLen; elPaso(); } }
    if (kAmp > 0.001f) { kPitF = kPit + (kPitF - kPit) * 0.9985f; kPh += incF(kPitF); elD += sat3(sinPh(kPh) * 1.4f) * kAmp * 0.9f; kAmp *= kDec; }
    if (hAmp > 0.001f) { elD += svfBP(hF, hK, ruido()) * hAmp * 0.55f; hAmp *= hDec; }
    kDuck *= 0.99983f;
    if (elAmp > 0.0005f || elGate) {
      elIncF += (elIncT - elIncF) * 0.003f;
      uint32_t inc = (uint32_t)elIncF; elPh += inc; elPhS += inc >> 1;
      float o;
      if (elTim == 0) o = leeTabla(tabSaw[elMip], elPh) * 0.8f;
      else if (elTim == 1) o = (leeTabla(tabSaw[elMip], elPh) - leeTabla(tabSaw[elMip], elPh + 0x80000000u)) * 0.5f;
      else if (elTim == 2) { elPhM += (uint32_t)(elIncF * 2.76f); float m = sinPh(elPhM); o = sinPh(elPh + (uint32_t)(int32_t)(m * (0.8f + 2.0f * elFenv) * 6.8e8f)) * 0.8f; }
      else o = leeTabla(tabSaw[elMip], elPh) * 0.7f + ruido() * 0.25f;
      o = sat3((o + sinPh(elPhS) * 0.55f) * 1.3f);
      o = svfLP(elF, elR.cur, o + 1e-18f);
      if (elGate) elAmp += (elAmpT - elAmp) * 0.02f; else elAmp *= 0.9993f;
      elFenv *= 0.99992f;
      elO = o * elAmp * elLv * (1.0f - 0.55f * kDuck);
      acE += fabsf(elO);
    } else { elAmp = 0; }
    float xl = 0, xr_ = 0;
    if (vtAmp > 0.0008f || vtOn) {
      float n1 = ruido(), n2 = ruido(); float tt = 0;
      if (vtTone > 0.001f) { vtPh += incF(vtFreq); tt = sinPh(vtPh) * vtTone * vtG; }
      xl = (svfBP(vtFL, vtR.cur, n1) * vtAmp + tt) * vtGL; xr_ = (svfBP(vtFR, vtR.cur, n2) * vtAmp + tt) * vtGR;
    }
    if (rpAmp > 0.0008f) {
      rpAmp *= 0.99997f;
      xl += svfBP(rpFL, rpR.cur, ruido()) * rpAmp * 0.6f; xr_ += svfBP(rpFR, rpR.cur, ruido()) * rpAmp * 0.6f;
    }
    float inL = hum + th + ml + vl, inR2 = hum + th + mr + vr;
    float indL = svfLP(inLs, inR.cur, inL + 1e-18f), indR = svfLP(inRs, inR.cur, inR2 + 1e-18f);
    float dk = (1.0f - 0.40f * tAmp) * (1.0f - 0.60f * (rupDuck > 1.0f ? 1.0f : rupDuck)) * (1.0f - 0.45f * kDuck);
    MK(3)
    // — sumas por fuente (con sus volúmenes de capa) —
    float pianL = pl * gP * dk, pianR = pr * gP * dk;
    float ambL = (bl + dl) * gA * dk, ambR = (br + dr) * gA * dk;
    float induL = indL * gI, induR = indR * gI;
    float tonL = rl2 * 0.8f + im + xl, tonR = rr2 * 0.8f + im + xr_;
    float elL = (elO + elD * elLv * 2.0f) * dk, elR2 = elL;
    float dryL = pianL + ambL + induL + tonL + elL + micL * micLvS * dk, dryR = pianR + ambR + induR + tonR + elR2 + micR * micLvS * dk;
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
    float sEco = ((pianL + pianR) * 0.30f + (bl + br) * gA * 0.45f + (ml + mr) * gI * 0.30f + (vl + vr) * gI * 0.15f + (elL + elR2) * 0.35f + micSE * micLvS * micWet) * (0.15f + 1.2f * ecoAS) * fxG;
    float wL = sEco + lzR * ecoFb, wR = sEco * 0.9f + lzL * ecoFb;
    if (wL > 0.8f || wL < -0.8f) wL = limitador(wL);
    if (wR > 0.8f || wR < -0.8f) wR = limitador(wR);
    ecoL[ecoW] = (int16_t)(wL * 30000.0f); ecoR[ecoW] = (int16_t)(wR * 30000.0f);
    if (++ecoW >= ECO_N) ecoW = 0;
    float retEL = lzL * fxRs * 0.9f, retER = lzR * fxRs * 0.9f;
    MK(4)
    // — placa (Dattorro) a media tasa —
    xAcc += (pianL + pianR) * 0.22f + (bl + br + (dl + dr) * 0.3f) * gA * 0.38f + (ml + mr + vl + vr) * gI * 0.28f + (rl2 + rr2) * 0.4f + im * 0.5f + (xl + xr_) * vtSend * 0.6f + (elL + elR2) * 0.20f + micSR * micLvS * micWet;
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
    MK(5)
    float rvL = 0.5f * (pLo + pLn) * revRet, rvR = 0.5f * (pRo + pRn) * revRet;
    // — master: color → pasa-altos → ganancia → limitador → techo de agudos —
    float restL = dryL - ambL + retEL, restR = dryR - ambR + retER;
    float atL = ambL + rvL, atR = ambR + rvR;
    if (orbS > 0.002f) {                                              // ÓRBITA: el campo atmosférico gira despacio entre izquierda y derecha
      orbPh += orbInc; float ph = sinPh(orbPh) * orbS; float cs = 1.0f - 0.5f * ph * ph, sn = ph;
      float a = atL * cs - atR * sn, b = atL * sn + atR * cs; atL = a; atR = b;
    }
    if (shiS > 0.004f && !lite) {                                     // SHIMMER: octava arriba (dos granos con ventana triangular) hacia la reverb
      float sx = (ambL + ambR + pianL + pianR + (elL + elR2) * 0.5f) * 0.25f;
      shBuf[shW] = sx;
      float p2 = shPh + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
      float r1 = (float)shW - (1.0f - shPh) * 1024.0f; if (r1 < 0) r1 += 2048.0f;
      float r2 = (float)shW - (1.0f - p2) * 1024.0f; if (r2 < 0) r2 += 2048.0f;
      int i1 = (int)r1, i2 = (int)r2; float f1 = r1 - (float)i1, f2 = r2 - (float)i2;
      float y1 = shBuf[i1] + (shBuf[(i1 + 1) & 2047] - shBuf[i1]) * f1, y2 = shBuf[i2] + (shBuf[(i2 + 1) & 2047] - shBuf[i2]) * f2;
      float w1 = 1.0f - fabsf(2.0f * shPh - 1.0f), w2 = 1.0f - fabsf(2.0f * p2 - 1.0f);
      xAcc += (y1 * w1 + y2 * w2) * shiS * 1.6f;
      shPh += 1.0f / 1024.0f; if (shPh >= 1.0f) shPh -= 1.0f;
      shW = (shW + 1) & 2047;
    }
    float mL = restL + atL, mR = restR + atR;
    if (widS > 0.01f) {                                               // ANCHO: lado ampliado + una copia retrasada del centro metida en el lado
      float M = (mL + mR) * 0.5f, S = (mL - mR) * 0.5f;
      wBuf[wI] = M; float Md = wBuf[(wI - 530) & 1023]; wI = (wI + 1) & 1023;
      S = S * (1.0f + 1.2f * widS) + 0.35f * widS * Md;
      mL = M + S; mR = M - S;
    } else { wBuf[wI] = (mL + mR) * 0.5f; wI = (wI + 1) & 1023; }
    {                                                                  // ENSEMBLE: coro estéreo suave
      chL[chW] = mL; chR[chW] = mR;
      if (ensS > 0.004f && !lite) {
        chPh1 += 52000; chPh2 += 44000;
        float d1 = 820.0f + 120.0f * sinPh(chPh1), d2 = 820.0f + 120.0f * sinPh(chPh2);
        float r1 = (float)chW - d1; if (r1 < 0) r1 += 1024.0f; float r2 = (float)chW - d2; if (r2 < 0) r2 += 1024.0f;
        int a = (int)r1, b = (int)r2; float fa = r1 - (float)a, fb = r2 - (float)b;
        float wl = chL[a] + (chL[(a + 1) & 1023] - chL[a]) * fa, wr = chR[b] + (chR[(b + 1) & 1023] - chR[b]) * fb;
        mL = mL * (1.0f - 0.2f * ensS) + wl * 0.55f * ensS; mR = mR * (1.0f - 0.2f * ensS) + wr * 0.55f * ensS;
      }
      chW = (chW + 1) & 1023;
    }
    mL = svfLP(coL, coRa.cur, mL + 1e-18f); mR = svfLP(coR, coRa.cur, mR + 1e-18f);
    hpL += hpK * (mL - hpL); mL -= hpL; hpR += hpK * (mR - hpR); mR -= hpR;
    const float gain = 0.80f * vMs;
#if DIAG
    { float a = fabsf(mL * gain); if (a > dgPk) dgPk = a; dgN++; if (a > 0.75f) dgSobre++; }
#endif
    if (!(mL > -16.0f && mL < 16.0f && mR > -16.0f && mR < 16.0f)) {
#if DIAG
      { auto ok = [](float v) { return v > -16.0f && v < 16.0f; };
        g_bad = (ok(pianL) ? 0 : 1) | (ok(ambL) ? 0 : 2) | (ok(induL) ? 0 : 4) | (ok(elL) ? 0 : 8) | (ok(retEL) ? 0 : 16) | (ok(rvL) ? 0 : 32) | (ok(micL) ? 0 : 64) | (ok(tonL) ? 0 : 128) | (ok(restL) ? 0 : 256) | (ok(atL) ? 0 : 512); }
#endif
      sanar(); mL = mR = 0.0f; }
    {                                                                 // AUTO-NIVEL: compresor suave (umbral 0,5) para que ninguna combinación de capas sature
      float pk = fabsf(mL * gain), pr2 = fabsf(mR * gain); if (pr2 > pk) pk = pr2;
      cmpEnv += (pk - cmpEnv) * (pk > cmpEnv ? 0.02f : 0.00008f);
      if ((i & 7) == 0) cmpGT = (cmpEnv > 0.5f) ? 0.5f / cmpEnv : 1.0f;
      cmpGan += (cmpGT - cmpGan) * (cmpGT < cmpGan ? 0.02f : 0.0004f);
    }
    float oL2 = limitador(mL * gain * cmpGan), oR2 = limitador(mR * gain * cmpGan);
    lpOL += 0.78f * (oL2 - lpOL); lpOR += 0.78f * (oR2 - lpOR); oL2 = lpOL; oR2 = lpOR;
    buffer[i * 2]     = (int32_t)lrintf(oL2 * 8200000.0f) << 8;
    buffer[i * 2 + 1] = (int32_t)lrintf(oR2 * 8200000.0f) << 8;
    MK(6)
    acP += fabsf(pl); acI += fabsf(th) + fabsf(ml) + fabsf(vl); acA += fabsf(bl) + fabsf(dl);
  }
  const float inv = 1.0f / BUFFER_SAMPLES;
  g_pian = cl01(acP * inv * 6.0f); g_ind = cl01(acI * inv * 8.0f); g_amb = cl01(acA * inv * 6.0f);
  g_gr = 1.0f - cmpGan;
  g_el = cl01(acE * inv * 10.0f); g_pulE *= 0.93f;
  g_pul *= 0.93f; g_pulI *= 0.93f; g_pulA *= 0.96f;
}

IRAM_ATTR void renderBuffer() {
  int32_t buffer[BUFFER_SAMPLES * 2];
  { static int32_t micRaw[BUFFER_SAMPLES * 2];
    if (runMask & 32) {
      if (!rxEn) { i2s_channel_enable(rx_chan); rxEn = true; }
      size_t got = 0; i2s_channel_read(rx_chan, micRaw, sizeof(micRaw), &got, 3);
      int fr = (int)(got / 8);
      for (int i = 0; i < BUFFER_SAMPLES; i++) micIn[i] = i < fr ? (float)(micRaw[2 * i] >> 8) * (1.0f / 8388608.0f) : 0.0f;
    } else if (rxEn) { i2s_channel_disable(rx_chan); rxEn = false; }
  }
  uint32_t t0 = micros();
  renderCore(buffer);
  uint32_t dt = micros() - t0;
  g_usAvg = (g_usAvg * 15 + dt) / 16;
  if (g_usAvg > (BUF_US * 84) / 100) g_lite = true; else if (g_usAvg < (BUF_US * 70) / 100) g_lite = false;
  if (dt > g_usMax) g_usMax = dt;
  if (dt > BUF_US - 150) { xrunAt = millis(); g_xruns = g_xruns + 1; }
  size_t written;
  i2s_channel_write(tx_chan, buffer, sizeof(buffer), &written, portMAX_DELAY);
}
void mic_init() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4; cc.dma_frame_num = 128;
  ESP_ERROR_CHECK(i2s_new_channel(&cc, NULL, &rx_chan));
  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)MIC_SCK, .ws = (gpio_num_t)MIC_WS, .dout = I2S_GPIO_UNUSED, .din = (gpio_num_t)MIC_SD, .invert_flags = { false, false, false } },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &sc));
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
// ESTADOS: 8 mezclas de fábrica (los 16 valores de las 4 capas + escala + estilo + interruptores). Cambiar de estado DESLIZA los valores.
// ==============================================================================================================================================
struct Estado { const char *nom; float v[NV]; uint8_t esc, estilo; uint32_t fl; };
#define TC(tipo, col) (((tipo) << TIPO_SH) | ((col) << COL_SH))
#define ET(tim, mod) (((tim) << ETIM_SH) | ((mod) << EMOD_SH))
// v = piano(4) · industrial(4) · ambiente(4) · efectos(4) · electro(4: volumen, tempo, filtro, mutación) · inmersión(4: volumen, órbita, ancho, shimmer)
const Estado ESTADOS[8] = {
  { "CALMA",         { 0.80f,0.40f,0.30f,0.35f,  0.35f,0.30f,0.20f,0.65f,  0.45f,0.30f,0.35f,0.30f,  0.70f,0.40f,0.55f,0.35f,  0.20f,0.25f,0.25f,0.10f,  0.55f,0.25f,0.40f,0.35f,  0.50f,0.40f,0.50f,0.35f }, 0, 0, FL_DRON | FL_NUBE | FL_ENS | TC(0, 1) | ET(0, 0) },
  { "NIEBLA",        { 0.65f,0.30f,0.20f,0.25f,  0.30f,0.25f,0.15f,0.80f,  0.65f,0.25f,0.25f,0.50f,  0.80f,0.35f,0.80f,0.50f,  0.30f,0.20f,0.20f,0.15f,  0.70f,0.20f,0.65f,0.60f,  0.50f,0.30f,0.35f,0.60f }, 1, 2, FL_DRON | FL_NUBE | FL_ORB | FL_ENS | TC(1, 2) | ET(2, 1)  | (1u << PTIM_SH)},
  { "FABRICA DIST.", { 0.65f,0.35f,0.30f,0.30f,  0.60f,0.40f,0.55f,0.55f,  0.30f,0.30f,0.30f,0.25f,  0.65f,0.45f,0.55f,0.40f,  0.50f,0.40f,0.45f,0.35f,  0.45f,0.30f,0.45f,0.25f,  0.45f,0.50f,0.55f,0.40f }, 2, 0, FL_PULSO | FL_METAL | FL_DRON | FL_ENS | TC(0, 1) | ET(3, 3) | (2u << ERIT_SH) | (5u << PTIM_SH)},
  { "NOCHE",         { 0.85f,0.25f,0.20f,0.20f,  0.25f,0.20f,0.10f,0.75f,  0.40f,0.25f,0.20f,0.35f,  0.75f,0.35f,0.70f,0.45f,  0.35f,0.22f,0.30f,0.20f,  0.60f,0.20f,0.55f,0.50f,  0.50f,0.30f,0.30f,0.50f }, 4, 0, FL_DRON | FL_NUBE | FL_ORB | FL_ENS | TC(2, 2) | ET(0, 0)  | (1u << PTIM_SH)},
  { "DERIVA",        { 0.75f,0.50f,0.50f,0.50f,  0.40f,0.40f,0.35f,0.50f,  0.50f,0.45f,0.50f,0.45f,  0.60f,0.50f,0.50f,0.45f,  0.45f,0.45f,0.50f,0.50f,  0.55f,0.35f,0.50f,0.40f,  0.50f,0.45f,0.50f,0.45f }, 3, 3, FL_PULSO | FL_DRON | FL_NUBE | FL_ORB | FL_ENS | TC(0, 0) | ET(1, 2) | (1u << ERIT_SH) | (2u << PTIM_SH)},
  { "CRISTAL",       { 0.70f,0.60f,0.75f,0.55f,  0.15f,0.30f,0.10f,0.30f,  0.45f,0.55f,0.80f,0.65f,  0.55f,0.40f,0.65f,0.50f,  0.35f,0.50f,0.60f,0.40f,  0.65f,0.30f,0.70f,0.65f,  0.45f,0.55f,0.70f,0.50f }, 5, 1, FL_NUBE | FL_DRON | FL_ORB | FL_ENS | TC(0, 3) | ET(2, 1)  | (3u << PTIM_SH)},
  { "MAQUINA",       { 0.55f,0.35f,0.40f,0.35f,  0.75f,0.65f,0.70f,0.40f,  0.30f,0.35f,0.30f,0.25f,  0.45f,0.45f,0.45f,0.40f,  0.65f,0.55f,0.60f,0.60f,  0.40f,0.40f,0.40f,0.20f,  0.40f,0.60f,0.60f,0.30f }, 0, 2, FL_PULSO | FL_METAL | FL_DRON | FL_ENS | TC(3, 0) | ET(3, 3) | (3u << ERIT_SH) | (2u << PTIM_SH)},
  { "SUSPENSO",      { 0.60f,0.20f,0.25f,0.20f,  0.45f,0.20f,0.20f,0.75f,  0.70f,0.20f,0.40f,0.50f,  0.85f,0.30f,0.90f,0.60f,  0.40f,0.15f,0.20f,0.30f,  0.75f,0.15f,0.75f,0.70f,  0.55f,0.25f,0.30f,0.70f }, 7, 2, FL_DRON | FL_NUBE | FL_ORB | FL_ENS | TC(3, 2) | ET(0, 0)  | (5u << PTIM_SH)},
};
volatile int estadoAct = 0;
uint8_t slotUsr = 0;                                                  // bit n = existe /PIANO_PIANO/estadoN.txt (tu mezcla guardada ahí)

// ─── valores por capa: cada una guarda sus 4; las perillas entran con ESCALADO DE VALOR (nada salta) ───
float guardado[NCAPAS][4] = { { 0.80f, 0.40f, 0.30f, 0.35f }, { 0.35f, 0.30f, 0.20f, 0.65f }, { 0.45f, 0.30f, 0.35f, 0.30f }, { 0.70f, 0.40f, 0.55f, 0.35f }, { 0.80f, 0.333f, 0.40f, 0.50f }, { 0.70f, 0.40f, 0.50f, 0.0f }, { 0.20f, 0.25f, 0.25f, 0.10f }, { 0.55f, 0.25f, 0.40f, 0.35f }, { 0.60f, 0.40f, 0.55f, 0.40f } };
bool  primeraLectura[4] = { true, true, true, true };
bool  bloqueado[4] = { true, true, true, true };
bool  entrada[4] = { false, false, false, false };
float p0[4] = { 0, 0, 0, 0 };
float potUlt[4] = { -1, -1, -1, -1 };
float objetivo[NV]; bool deslizando[NV];
volatile int capaCtl = 0;
volatile bool menuAbierto = false; int menuSel = 0;
float flashLevel = 0.0f;
float meterV = 0.0f; unsigned long meterT = 0; uint8_t meterHue = 0;
unsigned long capaT = 0;
char toastBuf[26] = ""; unsigned long toastT = 0;
bool escenaCargada = false;
void toast(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(toastBuf, sizeof(toastBuf), fmt, ap); va_end(ap); toastT = millis(); }
inline void escribirParam(int capa, int p, float v) { par[capa][p] = v; }
void cambiarCapa(int n) {
  for (int q = 0; q < 4; q++) { bloqueado[q] = false; entrada[q] = true; }
  capaCtl = n; flashLevel = 0.9f; capaT = millis(); toastT = 0;
}
void leerPotCapa(int p, float v) {
  int capa = capaCtl;
  float out = v;
  if (primeraLectura[p]) {                                            // al encender, la capa en curso toma la perilla
    primeraLectura[p] = false; bloqueado[p] = true; guardado[capa][p] = v; escribirParam(capa, p, v);
  } else {
    if (entrada[p]) { entrada[p] = false; p0[p] = v; }
    if (!bloqueado[p]) {
      float v0 = guardado[capa][p], pp = p0[p];
      if (v >= pp) out = v0 + (v - pp) / (1.0f - pp > 0.02f ? 1.0f - pp : 0.02f) * (1.0f - v0);
      else         out = v0 * v / (pp > 0.02f ? pp : 0.02f);
      if (out < 0) out = 0; if (out > 1) out = 1;
      if (fabsf(v - out) < 0.03f) bloqueado[p] = true;
    }
    if (fabsf(out - guardado[capa][p]) > 0.0015f) {
      guardado[capa][p] = out; escribirParam(capa, p, out);
      { int fi = capa < 4 ? capa * 4 + p : (capa == 6 ? 16 + p : (capa == 7 ? 20 + p : -1)); if (fi >= 0) deslizando[fi] = false; }                 // si la tocas, el estado deja de moverla
      meterV = out; meterT = millis(); meterHue = (uint8_t)(capa * 50);
    }
  }
  bool alguno = false;
  for (int q = 0; q < 4; q++) if (!bloqueado[q]) alguno = true;
  g_sinRecoger = alguno;
}
void pasoDeslizar() {
  bool alguno = false;
  for (int i = 0; i < NV; i++) if (deslizando[i]) { alguno = true; break; }
  if (!alguno) return;
  float tt = 2.0f * powf(30.0f, guardado[4][2]);                      // 2 … 60 s
  float k = 3.0f / (tt * 1000.0f);
  for (int i = 0; i < NV; i++) if (deslizando[i]) {
    float *g = &guardado[capaDeIdx(i)][i & 3];
    float d = objetivo[i] - *g;
    if (fabsf(d) < 0.002f) { *g = objetivo[i]; deslizando[i] = false; } else *g += d * k;
    escribirParam(capaDeIdx(i), i & 3, *g);
  }
}
void iniciarDeslizar(const float *v) {
  for (int i = 0; i < NV; i++) { objetivo[i] = v[i]; deslizando[i] = ((i & 3) != 0); }   // los VOLUMENES (POT1 de cada capa) son tuyos: ningún estado los sube ni los baja
  for (int q = 0; q < 4; q++) { bloqueado[q] = false; entrada[q] = true; }
}
void aplicarFabrica(int n) {
  const Estado &e = ESTADOS[n];
  iniciarDeslizar(e.v);
  reqEsc = e.esc; reqEstilo = e.estilo;
  flagsA = (flagsA & (FL_CONG | FL_BYP | FL_SW)) | (e.fl & ~(uint32_t)FL_SW);   // un estado cambia el CARÁCTER, no enciende ni apaga fuentes (campanas, drón, pulso, metal)
  estadoAct = n; flashLevel = 1.0f;
}

// ─── escena en texto (autoguardado y estados del usuario comparten formato) ───
#define SD_DIR "/PIANO_PIANO"
#define SD_ARCH "/PIANO_PIANO/escena.txt"
#define SD_TMP "/PIANO_PIANO/escena.tmp"
static char escenaBuf[700], escenaUlt[700], escenaGuard[700], slotBuf[700];
int armarEscena(char *b, int n) {
  int k = snprintf(b, n, "v=1\ncapa=%d\n", (int)capaCtl);
  for (int c = 0; c < NCAPAS; c++) k += snprintf(b + k, n - k, "p%d=%.3f,%.3f,%.3f,%.3f\n", c, guardado[c][0], guardado[c][1], guardado[c][2], guardado[c][3]);
  k += snprintf(b + k, n - k, "esc=%u\nton=%u\nest=%u\nfl=%u\nestado=%d\nrun=%u\nvia=%u\nevo2=%u\nmic=%u\n", (unsigned)(reqEsc % NESC), (unsigned)(reqTon % 12), (unsigned)(reqEstilo & 3),
                (unsigned)(flagsA & ~(uint32_t)(FL_CONG | FL_BYP)), (int)estadoAct, (unsigned)(runMask & 0x1F), (unsigned)(reqViajeTipo & 3), (unsigned)(reqEvo & 3), (unsigned)((runMask >> 5) & 1));
  return k;
}
bool aplicarTexto(const char *txt, bool comoEstado) {
  int capa = 0, nCapas = 0; unsigned u; int i; float v[4];
  float nuevo[NV]; for (int q = 0; q < NV; q++) nuevo[q] = guardado[capaDeIdx(q)][q & 3];
  for (const char *l = txt; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
    if (sscanf(l, "capa=%d", &i) == 1) capa = (i < 0 || i >= NCAPAS) ? 0 : i;
    else if (l[0] == 'p' && sscanf(l, "p%d=%f,%f,%f,%f", &i, &v[0], &v[1], &v[2], &v[3]) == 5 && i >= 0 && i < NCAPAS) {
      for (int q = 0; q < 4; q++) { if (v[q] < 0) v[q] = 0; if (v[q] > 1) v[q] = 1; }
      if (i < 4 || i >= 6) { int b = i < 4 ? i * 4 : 16 + (i - 6) * 4; for (int q = 0; q < 4; q++) nuevo[b + q] = v[q]; }
      else if (!comoEstado) { for (int q = 0; q < 4; q++) { guardado[i][q] = v[q]; escribirParam(i, q, v[q]); } }
      nCapas++;
    }
    else if (sscanf(l, "esc=%u", &u) == 1) reqEsc = u % NESC;
    else if (sscanf(l, "ton=%u", &u) == 1) { if (!comoEstado) reqTon = u % 12; }
    else if (sscanf(l, "est=%u", &u) == 1) reqEstilo = u & 3;
    else if (sscanf(l, "fl=%u", &u) == 1) { const uint32_t keep = FL_CONG | FL_BYP | (comoEstado ? (uint32_t)FL_SW : 0u); flagsA = (flagsA & keep) | (u & ~keep); }
    else if (sscanf(l, "estado=%d", &i) == 1) { if (!comoEstado && i >= 0 && i < 8) estadoAct = i; }
    else if (sscanf(l, "run=%u", &u) == 1) { if (!comoEstado) runMask = (uint8_t)((u & 0x1F) | (runMask & 32)); }
    else if (sscanf(l, "mic=%u", &u) == 1) { if (!comoEstado) runMask = (uint8_t)((runMask & 0x1F) | (u ? 32 : 0)); }
    else if (sscanf(l, "via=%u", &u) == 1) reqViajeTipo = u & 3;
    else if (sscanf(l, "evo2=%u", &u) == 1) { if (!comoEstado) reqEvo = u & 3; }
  }
  if (nCapas < 4) return false;
  if (comoEstado) { iniciarDeslizar(nuevo); }
  else {
    for (int q = 0; q < NV; q++) { guardado[capaDeIdx(q)][q & 3] = nuevo[q]; escribirParam(capaDeIdx(q), q & 3, nuevo[q]); }
    capaCtl = capa;
    for (int q = 0; q < 4; q++) { primeraLectura[q] = false; bloqueado[q] = false; entrada[q] = true; }
  }
  return true;
}

// ==============================================================================================================================================
// MICROSD (SCK 14 · MOSI 15 · MISO 16 · CS 17; FAT32): la escena se guarda sola 3 s después del último cambio (temporal + renombrar)
// ==============================================================================================================================================
#define SD_SCK  14
#define SD_MISO 16
#define SD_MOSI 15
#define SD_CS   17
SPIClass sdSpi(FSPI);
SemaphoreHandle_t sdMx;
uint32_t sdFrecOk = 0;
volatile uint8_t reqSlotGuardar = 0;                                  // 1..8: guardar la mezcla actual en estadoN (lo hace la tarea de SD)
bool sdMontar() {
  static const uint32_t FREC[4] = { 20000000, 10000000, 4000000, 1000000 };
  for (int i = 0; i < 4; i++) {
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, sdSpi, FREC[i])) { sdFrecOk = FREC[i]; if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR); return true; }
    SD.end(); sdSpi.end();
  }
  return false;
}
void sdSlotsEscanear() {
  slotUsr = 0;
  char p[40];
  for (int n = 0; n < 8; n++) { snprintf(p, sizeof(p), SD_DIR "/estado%d.txt", n); if (SD.exists(p)) slotUsr |= (1 << n); }
}
void sdIniciar() {
  if (!sdMontar()) { g_sd = 0;
#if DIAG
    DBG_PRINTLN("SD: sin tarjeta");
#endif
    return; }
  g_sd = 1;
  sdSlotsEscanear();
  File f = SD.open(SD_ARCH);
  if (f) {
    size_t n = f.read((uint8_t *)escenaBuf, sizeof(escenaBuf) - 1); escenaBuf[n] = 0; f.close();
    if (aplicarTexto(escenaBuf, false)) { g_sd = 3; escenaCargada = true; }
  }
  armarEscena(escenaGuard, sizeof(escenaGuard)); strcpy(escenaUlt, escenaGuard);
  if (g_sd == 1) escenaGuard[0] = 0;
#if DIAG
  DBG_PRINTF("SD: lista a %lu kHz | escena %s | estados de usuario 0x%02X\n", (unsigned long)(sdFrecOk / 1000), g_sd == 3 ? "RECUPERADA" : "nueva", (unsigned)slotUsr);
#endif
}
void sdTask(void *) {
  unsigned long tCambio = 0;
  for (;;) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (!g_sd) { vTaskDelay(4500 / portTICK_PERIOD_MS); xSemaphoreTake(sdMx, portMAX_DELAY); bool ok = sdMontar(); if (ok) sdSlotsEscanear(); xSemaphoreGive(sdMx); if (ok) { g_sd = 1; escenaGuard[0] = 0; } continue; }
    if (reqSlotGuardar) {
      int n = reqSlotGuardar - 1; reqSlotGuardar = 0;
      char p[40]; snprintf(p, sizeof(p), SD_DIR "/estado%d.txt", n);
      int k = armarEscena(slotBuf, sizeof(slotBuf));
      bool ok = false;
      xSemaphoreTake(sdMx, portMAX_DELAY);
      SD.remove(p);
      File f = SD.open(p, FILE_WRITE);
      if (f) { ok = (f.write((const uint8_t *)slotBuf, k) == (size_t)k); f.close(); }
      xSemaphoreGive(sdMx);
      if (ok) slotUsr |= (1 << n);
    }
    armarEscena(escenaBuf, sizeof(escenaBuf));
    if (strcmp(escenaBuf, escenaUlt) != 0) { strcpy(escenaUlt, escenaBuf); tCambio = millis(); }
    else if (strcmp(escenaBuf, escenaGuard) != 0 && millis() - tCambio > 3000) {
      bool ok = false;
      xSemaphoreTake(sdMx, portMAX_DELAY);
      File f = SD.open(SD_TMP, FILE_WRITE);
      if (f) { size_t n = strlen(escenaBuf); ok = (f.write((const uint8_t *)escenaBuf, n) == n); f.close(); }
      if (ok) { SD.remove(SD_ARCH); ok = SD.rename(SD_TMP, SD_ARCH); }
      xSemaphoreGive(sdMx);
      if (ok) { strcpy(escenaGuard, escenaBuf); g_sd = (g_sd == 3) ? 3 : 1;
#if DIAG
        DBG_PRINTLN("SD: escena guardada");
#endif
      } else { g_sd = 0; SD.end(); }
    }
  }
}
// carga el estado n: si tienes tu propia mezcla guardada ahí, esa; si no, la de fábrica
void cargarEstado(int n) {
  bool hecho = false;
  if (g_sd && (slotUsr & (1 << n))) {
    char p[40]; snprintf(p, sizeof(p), SD_DIR "/estado%d.txt", n);
    xSemaphoreTake(sdMx, portMAX_DELAY);
    File f = SD.open(p);
    if (f) { size_t k = f.read((uint8_t *)slotBuf, sizeof(slotBuf) - 1); slotBuf[k] = 0; f.close(); hecho = true; }
    xSemaphoreGive(sdMx);
    if (hecho) hecho = aplicarTexto(slotBuf, true);
  }
  if (!hecho) aplicarFabrica(n);
  estadoAct = n; flashLevel = 1.0f;
}

// ==============================================================================================================================================
// LEDs, PANTALLA, CONTROL
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
static const uint8_t HUE_CAPA[NCAPAS] = { 150, 20, 96, 200, 60, 0, 130, 215, 100 };    // PIANO azul-violeta · INDUSTRIAL naranja · AMBIENTE verde · EFECTOS magenta · ESTADOS amarillo
#define NORD 8                                                       // capas visibles (VIAJE ya no existe en el ciclo)
static const uint8_t ORDEN[NORD] = { 0, 6, 1, 2, 7, 8, 3, 4 };   // orden de BTN1: PIANO · ELECTRO · INDUSTRIAL · AMBIENTE · INMERSION · EFECTOS · VIAJE · ESTADOS
int posDe(int capa) { for (int i = 0; i < NORD; i++) if (ORDEN[i] == capa) return i; return 0; }
bool estaCorriendo(int capa) { return capa < 3 ? ((runMask >> capa) & 1) : (capa == 5 ? g_viaje : (capa == 6 ? ((runMask >> 3) & 1) : (capa == 7 ? ((runMask >> 4) & 1) : (capa == 8 ? ((runMask >> 5) & 1) : ((runMask & 0x1F) != 0))))); }
static inline bool run_mic() { return (runMask & 32) != 0; }
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
  } else if (capa == 0) {
    leds[0] = CHSV(hu, 255, (uint8_t)(40 + g_pul * 200.0f));
    leds[1] = CHSV(hu, 255, (uint8_t)(15 + g_pian * 240.0f));
    leds[2] = CHSV((uint8_t)(g_esc * 32), 255, 90);                   // escala: un color por escala
    leds[3] = CHSV((uint8_t)(g_ton * 21), 255, 90);                   // tónica: un color por nota
    leds[4] = CHSV((uint8_t)(g_estilo * 48 + 10), 255, 80);
    leds[3] = CHSV((uint8_t)(((flagsA >> PTIM_SH) & 7) * 40 + 5), 200, 110);
  } else if (capa == 1) {
    leds[0] = CHSV(hu, 255, (uint8_t)(40 + g_pulI * 200.0f));
    leds[1] = CHSV(hu, 255, (uint8_t)(15 + g_ind * 240.0f));
    uint32_t fl = flagsA;
    leds[2] = (fl & FL_PULSO) ? CHSV(0, 255, 160) : CRGB(3, 0, 0);
    leds[3] = (fl & FL_METAL) ? CHSV(40, 120, 160) : CRGB(3, 3, 0);
    leds[4] = CHSV(140, 120, (uint8_t)(6 + 240.0f * (stAmp > 1.0f ? 1.0f : stAmp)));
  } else if (capa == 2) {
    leds[0] = CHSV(hu, 255, (uint8_t)(40 + g_pulA * 200.0f));
    leds[1] = CHSV(hu, 255, (uint8_t)(15 + g_amb * 240.0f));
    uint32_t fl = flagsA;
    leds[2] = (fl & FL_DRON) ? CHSV(96, 255, 150) : CRGB(0, 3, 0);
    leds[3] = (fl & FL_NUBE) ? CHSV(130, 255, 150) : CRGB(0, 2, 3);
    leds[4] = CHSV((uint8_t)(((flagsA >> TIPO_SH) & 3) * 50), 255, 80);
  } else if (capa == 3) {
    uint32_t fl = flagsA;
    leds[0] = CHSV(hu, 255, (fl & FL_BYP) ? 10 : 90);
    leds[1] = (fl & FL_CONG) ? CRGB(120, 200, 255) : CRGB(2, 3, 4);
    leds[2] = CHSV((uint8_t)(((fl >> COL_SH) & 3) * 40 + 20), 255, 110);
    leds[3] = (fl & FL_ECO2) ? CRGB(255, 160, 0) : CRGB(4, 3, 0);
    leds[4] = CHSV(hu, 255, (uint8_t)(10 + 200.0f * guardado[3][3]));
  } else if (capa == 6) {
    leds[0] = CHSV(hu, 255, (uint8_t)(40 + g_pulE * 200.0f));
    leds[1] = CHSV(hu, 255, (uint8_t)(15 + g_el * 240.0f));
    leds[2] = CHSV((uint8_t)(((flagsA >> ETIM_SH) & 3) * 45 + 10), 255, 130);
    leds[3] = CHSV((uint8_t)(((flagsA >> EMOD_SH) & 3) * 45 + 100), 255, 110);
    leds[4] = CHSV(0, 255, (uint8_t)(6 + 230.0f * guardado[6][3] * guardado[6][3]));
  } else if (capa == 8) {
    uint32_t fl = flagsA;
    leds[0] = CHSV(hu, 255, (run_mic() ? 120 : 20));
    leds[1] = CHSV(hu, 255, (uint8_t)(6 + 249.0f * cl01(g_micEnv * 8.0f)));
    leds[2] = (fl & MCON) ? CRGB(120, 200, 255) : CRGB(2, 3, 4);
    leds[3] = CHSV((uint8_t)(((fl >> MUSO_SH) & 3) * 50 + 20), 255, 130);
    leds[4] = (fl & MINF) ? CHSV(30, 255, 190) : CRGB(3, 2, 0);
  } else if (capa == 7) {
    uint32_t fl = flagsA;
    leds[0] = CHSV(hu, 255, 90);
    leds[1] = (fl & FL_ORB) ? CHSV(140, 255, 170) : CRGB(0, 2, 3);
    leds[2] = (fl & FL_SHI) ? CHSV(200, 120, 190) : CRGB(3, 0, 3);
    leds[3] = (fl & FL_ENS) ? CHSV(30, 200, 170) : CRGB(3, 2, 0);
    leds[4] = CHSV(hu, 255, (uint8_t)(10 + 200.0f * guardado[7][2]));
  } else if (capa == 5) {
    leds[0] = CHSV(hu, 255, g_viaje ? 230 : 60);
    leds[1] = CHSV(0, 255, (uint8_t)(10 + 240.0f * cl01(rupDuck)));
    leds[2] = CHSV((uint8_t)(reqViajeTipo * 50 + 10), 255, 140);
    leds[3] = g_viaje ? CRGB(255, 255, 255) : CRGB(4, 4, 4);
    leds[4] = CHSV(0, 255, (uint8_t)(10 + 200.0f * guardado[5][3]));
  } else {
    leds[0] = CHSV(hu, 255, 120);
    for (int i = 0; i < 4; i++) leds[1 + i] = (i < 4 && (estadoAct >> 1) == i) ? CHSV(hu, 255, (estadoAct & 1) ? 120 : 230) : CRGB(3, 3, 0);
  }
  if (!medidor && !estaCorriendo(capa)) leds[0] = CRGB(40, 0, 0);
  if (menuAbierto) { for (int i = 0; i < 5; i++) leds[i] = ((t / 200 + i) & 1) ? CHSV(200, 255, 200) : CHSV(200, 255, 20); }
  if (g_sinRecoger) leds[5] = CRGB(255, 120, 0); else leds[5] = g_sd ? CRGB(0, 8, 0) : CRGB(8, 8, 8);
  if (xrunAt != 0 && t - xrunAt < 1500) leds[5] = CRGB(255, 0, 0);
  onboard[0] = CHSV(hu, 255, 20 + (uint8_t)(g_pian * 120.0f + g_amb * 80.0f));
  onboard[0].nscale8(ONBOARD_BRIGHT);
  if (flashLevel > 0.02f) { uint8_t w = (uint8_t)(flashLevel * 130.0f); for (int i = 0; i < NUM_LEDS; i++) leds[i] += CRGB(w, w, w); flashLevel *= 0.62f; }
  ledsEscribir();
}

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
#if DIAG
    dgPag++;
#endif
    vTaskDelay(1);
  }
  return true;
}

static const char *CAPA_NOM[NCAPAS] = { "PIANO", "INDUSTRIAL", "AMBIENTE", "EFECTOS", "ESTADOS", "VIAJE", "ELECTRO", "INMERSION", "MICRO" };
static const char *POT_NOM[NCAPAS][4] = {
  { "VOLUMEN", "TEMPO", "TOQUE", "NOTAS" }, { "VOLUMEN", "PULSO", "DENSIDAD", "OSCURIDAD" }, { "VOLUMEN", "RITMO NUBE", "BRILLO", "NUBE" },
  { "RETORNO", "ECO BPM", "REVERB", "ECO" }, { "MASTER", "TEMPO GLOB", "TRANSIC.", "INTENSID." }, { "VOLUMEN", "DURACION", "FUERZA", "AZAR" }, { "VOLUMEN", "TEMPO", "FILTRO", "MUTACION" }, { "VOLUMEN", "ORBITA", "ANCHO", "SHIMMER" }, { "VOLUMEN", "TIEMPO", "FILTRO", "EFECTO" } };
static const char *BTN_NOM[NCAPAS][3] = { { "ESCALA", "SONIDO", "ESTILO" }, { "PULSO", "METAL", "GOLPE" }, { "DRON", "NUBE", "TONICA" }, { "CONGELA", "COLOR", "ECO 1/2" }, { "ANTERIOR", "SIGUIE.", "EVOLUC." }, { "RUPTURA", "TIPO", "VIAJE" }, { "RITMO", "TIMBRE", "MODO" }, { "ORBITA", "SHIMMER", "ENSEMBL" }, { "CONGELA", "USO", "INFLUYE" } };
void oDibujar() {
  memset(fb, 0, sizeof(fb));
  unsigned long t = millis();
  int capa = capaCtl; char b[40];
  bool run = estaCorriendo(capa);
  if (menuAbierto) {                                                  // MENÚ DE ESTADOS (BTN1 mantenido)
    oRect(0, 0, 128, 9, true); oText(2, 1, "ESTADOS (* = TUYO)", 1, true);
    int ini = menuSel - 2; if (ini > 3) ini = 3; if (ini < 0) ini = 0;
    for (int r = 0; r < 5; r++) {
      int i = ini + r; bool sel = (i == menuSel);
      if (sel) oRect(0, 11 + r * 9, 128, 9, true);
      snprintf(b, sizeof(b), "%c%d %.13s%s%s", sel ? '>' : ' ', i + 1, ESTADOS[i].nom, (slotUsr >> i) & 1 ? " *" : "", i == estadoAct ? " <" : ""); oText(2, 12 + r * 9, b, 1, sel);
    }
    oRect(0, 54, 128, 10, true); oText(2, 56, "B2 GUARDA B5 CARGA B3/4", 1, true);
    return;
  }
  if (t - capaT < 1100 && capaT != 0) {                               // anuncio de capa
    snprintf(b, sizeof(b), "CAPA %d DE %d", posDe(capa) + 1, NORD); oTextC(4, b, 2);
    oTextC(26, CAPA_NOM[capa], strlen(CAPA_NOM[capa]) > 7 ? 2 : 3);
    oRect(0, 52, 128, 1, true);
    if (escenaCargada && t < 4500) snprintf(b, sizeof(b), "ESCENA CARGADA DE SD"); else snprintf(b, sizeof(b), "%s  %s %s", run ? "ACTIVA" : "APAGADA", ESC_COR[g_esc], TON_NOM[g_ton]);
    oTextC(55, b, 1);
    return;
  }
  oRect(0, 0, 128, 9, true);                                          // mapa de la capa
  snprintf(b, sizeof(b), "%d/%d %s", posDe(capa) + 1, NORD, CAPA_NOM[capa]); oText(2, 1, b, 1, true);
  snprintf(b, sizeof(b), "%s %s", ESC_COR[g_esc], TON_NOM[g_ton]);
  oText(128 - 2 - (int)strlen(b) * 6, 1, b, 1, true);
  for (int p = 0; p < 4; p++) {
    int y = 11 + p * 9; float v = guardado[capa][p];
    snprintf(b, sizeof(b), "P%d %s", p + 1, POT_NOM[capa][p]); oText(0, y, b);
    oRect(66, y + 1, 34, 6, true); oRect(67, y + 2, 32, 4, false);
    oRect(67, y + 2, (int)(32.0f * v + 0.5f), 4, true);
    if (!bloqueado[p]) { int xk = 67 + (int)(31.0f * potUlt[p]); oRect(xk, y - 1, 1, 9, true); }
    if (p == 1 && capa == 0) snprintf(b, sizeof(b), "%d", (int)(30.0f + 90.0f * v + 0.5f));
    else if (p == 1 && capa == 1) snprintf(b, sizeof(b), "%d", (int)(30.0f + 60.0f * v + 0.5f));
    else if (p == 1 && capa == 3) snprintf(b, sizeof(b), "%d", (int)(66.0f + 84.0f * v + 0.5f));
    else if (p == 1 && capa == 5) snprintf(b, sizeof(b), "%ds", (int)(2.0f + 12.0f * v + 0.5f));
    else if (p == 1 && capa == 6) snprintf(b, sizeof(b), "%d", (int)(60.0f + 90.0f * v + 0.5f));
    else snprintf(b, sizeof(b), "%d", (int)(v * 100.0f + 0.5f));
    oText(128 - (int)strlen(b) * 6, y, b);
  }
  // fila de botones: B2 siempre stop/play; B3..B5 según la capa, con su estado actual
  uint32_t fl = flagsA;
  char e3[9], e4[9], e5[9];
  const char *s3 = BTN_NOM[capa][0], *s4 = BTN_NOM[capa][1], *s5 = BTN_NOM[capa][2];
  snprintf(e3, sizeof(e3), "%.7s", s3); snprintf(e4, sizeof(e4), "%.7s", s4); snprintf(e5, sizeof(e5), "%.7s", s5);
  if (capa == 1) { snprintf(e3, sizeof(e3), "PULSO%c", (fl & FL_PULSO) ? '+' : '-'); snprintf(e4, sizeof(e4), "METAL%c", (fl & FL_METAL) ? '+' : '-'); snprintf(e5, sizeof(e5), "GOLPE"); }
  else if (capa == 2) { snprintf(e3, sizeof(e3), "DRON%c", (fl & FL_DRON) ? '+' : '-'); snprintf(e4, sizeof(e4), "NUBE%c", (fl & FL_NUBE) ? '+' : '-'); snprintf(e5, sizeof(e5), "%.6s", TON_NOM[g_ton]); }
  else if (capa == 3) { snprintf(e3, sizeof(e3), "CONG%c", (fl & FL_CONG) ? '+' : '-'); snprintf(e4, sizeof(e4), "%.7s", COL_NOM[(fl >> COL_SH) & 3]); }
  else if (capa == 0) { snprintf(e4, sizeof(e4), "%.7s", PTIM_NOM[((fl >> PTIM_SH) & 7) % 6]); snprintf(e5, sizeof(e5), "%.7s", EST_ESTILO[g_estilo & 3]); }
  else if (capa == 5) { snprintf(e4, sizeof(e4), "%.7s", VIA_NOM[reqViajeTipo & 3]); }
  else if (capa == 6) { snprintf(e3, sizeof(e3), "%.7s", ((flagsA >> ERIT_SH) & 3) ? ERIT_NOM[(flagsA >> ERIT_SH) & 3] : "SIN"); snprintf(e4, sizeof(e4), "%.7s", ETIM_NOM[(fl >> ETIM_SH) & 3]); snprintf(e5, sizeof(e5), "%.7s", EMOD_NOM[(fl >> EMOD_SH) & 3]); }
  else if (capa == 8) { snprintf(e3, sizeof(e3), "CONG%c", (fl & MCON) ? '+' : '-'); snprintf(e4, sizeof(e4), "%.7s", MUSO_NOM[(fl >> MUSO_SH) & 3]); snprintf(e5, sizeof(e5), "INFLU%c", (fl & MINF) ? '+' : '-'); }
  else if (capa == 4) { snprintf(e5, sizeof(e5), "EVO %.3s", EVO_NOM[reqEvo & 3]); }
  else if (capa == 7) { snprintf(e3, sizeof(e3), "ORBIT%c", (fl & FL_ORB) ? '+' : '-'); snprintf(e4, sizeof(e4), "SHIMM%c", (fl & FL_SHI) ? '+' : '-'); snprintf(e5, sizeof(e5), "ENSEM%c", (fl & FL_ENS) ? '+' : '-'); }
  snprintf(b, sizeof(b), "B2 %-4s B3 %-7s", run ? "STOP" : "PLAY", e3); oText(0, 47, b);
  snprintf(b, sizeof(b), "B4 %-7s B5 %-7s", e4, e5); oText(0, 55, b);
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
    if (ok) fallos = 0;
    else if (++fallos >= 3) { fallos = 0; vTaskDelay(3000 / portTICK_PERIOD_MS); memset(fbPrev, 0xA5, sizeof(fbPrev)); oInit(); }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ─── botones y perillas ───
unsigned long evoUlt = 0;
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
  // REGLAS FIJAS: BTN1 corto = capa siguiente · BTN1 mantenido = menú de estados · POT1 = volumen · BTN2 = stop/play · POT2 = tempo
  static unsigned long b1T = 0; static bool b1Largo = false;
  if (f[0]) { b1T = t; b1Largo = false; }                          // f[0] ya trae el antirrebote (y actualiza bT[0])
  if (b[0] == LOW && b1T && !b1Largo && t - b1T > 700) { b1Largo = true; menuAbierto = true; menuSel = estadoAct; flashLevel = 0.9f; }
  if (b[0] == HIGH && bL[0] == LOW && b1T) {
    if (!b1Largo) { if (menuAbierto) menuAbierto = false; else cambiarCapa(ORDEN[(posDe(capaCtl) + 1) % NORD]); }
    b1T = 0;
  }
  int capa = capaCtl;
  if (menuAbierto) {
    if (f[2] && menuSel > 0) menuSel--;
    if (f[3] && menuSel < 7) menuSel++;
    if (f[4]) { cargarEstado(menuSel); toast("ESTADO %s", ESTADOS[menuSel].nom); menuAbierto = false; }
    if (f[1]) {
      if (g_sd) { reqSlotGuardar = (uint8_t)(menuSel + 1); toast("GUARDADO EN %s", ESTADOS[menuSel].nom); flashLevel = 1.0f; }
      else toast("SIN SD: NO SE GUARDA");
    }
  } else {
    if (f[1]) {                                                       // BTN2: stop/play de la capa (en ESTADOS, de todo)
      bool parar = estaCorriendo(capa);
      if (capa < 3) runMask = runMask ^ (uint8_t)(1 << capa);
      else if (capa == 3) { flagsA = flagsA ^ FL_BYP; parar = !(flagsA & FL_BYP) ? true : false; toast((flagsA & FL_BYP) ? "EFECTOS: BYPASS" : "EFECTOS: ACTIVOS"); }
      else if (capa == 5) { if (g_viaje) reqViajeStopN = reqViajeStopN + 1; else reqViajeN = reqViajeN + 1; }
      else if (capa == 6) runMask = runMask ^ 8;
      else if (capa == 7) runMask = runMask ^ 16;
      else if (capa == 8) runMask = runMask ^ 32;
      else { runMask = parar ? (uint8_t)(runMask & 32) : (uint8_t)(runMask | 0x1F); }
      flashLevel = 0.6f;
      if (capa != 3) toast("%s %s", CAPA_NOM[capa], parar ? "STOP" : "PLAY");
    }
    if (capa == 0) {                                                  // PIANO
      if (f[2]) { reqEsc = (reqEsc + 1) % NESC; flashLevel = 0.7f; toast("ESCALA %s", ESC_NOM[reqEsc]); }
      if (f[3]) { int p = (int)(((flagsA >> PTIM_SH) & 7) + 1) % 6; flagsA = (flagsA & ~(uint32_t)(7u << PTIM_SH)) | ((uint32_t)p << PTIM_SH); flashLevel = 0.7f; toast("SONIDO %s", PTIM_NOM[p]); }
      if (f[4]) { reqEstilo = (reqEstilo + 1) & 3; flashLevel = 0.6f; toast("ESTILO %s", EST_ESTILO[reqEstilo]); }
    } else if (capa == 1) {                                           // INDUSTRIAL
      if (f[2]) { flagsA = flagsA ^ FL_PULSO; flashLevel = 0.6f; toast("PULSO %s", (flagsA & FL_PULSO) ? "SI" : "NO"); }
      if (f[3]) { flagsA = flagsA ^ FL_METAL; flashLevel = 0.6f; toast("METAL %s", (flagsA & FL_METAL) ? "SI" : "NO"); }
      if (f[4]) { reqVaporN = reqVaporN + 1; flashLevel = 0.8f; toast("GOLPE"); }
    } else if (capa == 2) {                                           // AMBIENTE
      if (f[2]) { flagsA = flagsA ^ FL_DRON; flashLevel = 0.6f; toast("DRON %s", (flagsA & FL_DRON) ? "SI" : "NO"); }
      if (f[3]) { flagsA = flagsA ^ FL_NUBE; flashLevel = 0.6f; toast("NUBE %s", (flagsA & FL_NUBE) ? "SI" : "NO"); }
      if (f[4]) { reqTon = (reqTon + 7) % 12; flashLevel = 0.7f; toast("TONICA %s", TON_NOM[reqTon]); }
    } else if (capa == 3) {                                           // EFECTOS
      if (f[2]) { flagsA = flagsA ^ FL_CONG; flashLevel = 0.9f; toast((flagsA & FL_CONG) ? "REVERB CONGELADA" : "REVERB LIBRE"); }
      if (f[3]) { int c = (int)(((flagsA >> COL_SH) & 3) + 1) & 3; flagsA = (flagsA & ~(uint32_t)(3 << COL_SH)) | ((uint32_t)c << COL_SH); flashLevel = 0.6f; toast("COLOR %s", COL_NOM[c]); }
      if (f[4]) { flagsA = flagsA ^ FL_ECO2; flashLevel = 0.6f; toast("ECO %s", (flagsA & FL_ECO2) ? "CORCHEA" : "PUNTILLO"); }
    } else if (capa == 6) {                                           // ELECTRO
      if (f[2]) { int r = (int)(((flagsA >> ERIT_SH) & 3) + 1) & 3; flagsA = (flagsA & ~(uint32_t)(3 << ERIT_SH)) | ((uint32_t)r << ERIT_SH); reqElGenN = reqElGenN + 1; flashLevel = 0.7f; toast("RITMO %s", ERIT_NOM[r]); }
      if (f[3]) { int t = (int)(((flagsA >> ETIM_SH) & 3) + 1) & 3; flagsA = (flagsA & ~(uint32_t)(3 << ETIM_SH)) | ((uint32_t)t << ETIM_SH); flashLevel = 0.6f; toast("TIMBRE %s", ETIM_NOM[t]); }
      if (f[4]) { int m = (int)(((flagsA >> EMOD_SH) & 3) + 1) & 3; flagsA = (flagsA & ~(uint32_t)(3 << EMOD_SH)) | ((uint32_t)m << EMOD_SH); flashLevel = 0.6f; toast("MODO %s", EMOD_NOM[m]); }
    } else if (capa == 8) {                                           // MICRO
      if (f[2]) { flagsA = flagsA ^ MCON; flashLevel = 0.7f; toast((flagsA & MCON) ? "MICRO CONGELADO" : "MICRO LIBRE"); }
      if (f[3]) { int u = (int)(((flagsA >> MUSO_SH) & 3) + 1) & 3; flagsA = (flagsA & ~(uint32_t)(3 << MUSO_SH)) | ((uint32_t)u << MUSO_SH); flashLevel = 0.6f; toast("USO %s", MUSO_NOM[u]); }
      if (f[4]) { flagsA = flagsA ^ MINF; flashLevel = 0.6f; toast("INFLUYE %s", (flagsA & MINF) ? "SI" : "NO"); }
    } else if (capa == 7) {                                           // INMERSION
      if (f[2]) { flagsA = flagsA ^ FL_ORB; flashLevel = 0.6f; toast("ORBITA %s", (flagsA & FL_ORB) ? "SI" : "NO"); }
      if (f[3]) { flagsA = flagsA ^ FL_SHI; flashLevel = 0.6f; toast("SHIMMER %s", (flagsA & FL_SHI) ? "SI" : "NO"); }
      if (f[4]) { flagsA = flagsA ^ FL_ENS; flashLevel = 0.6f; toast("ENSEMBLE %s", (flagsA & FL_ENS) ? "SI" : "NO"); }
    } else if (capa == 5) {                                           // VIAJE
      if (f[2]) { reqRupN = reqRupN + 1; flashLevel = 1.0f; toast("RUPTURA"); }
      if (f[3]) { reqViajeTipo = (reqViajeTipo + 1) & 3; flashLevel = 0.6f; toast("TIPO %s", VIA_NOM[reqViajeTipo]); }
      if (f[4]) { reqViajeN = reqViajeN + 1; flashLevel = 0.9f; toast("VIAJE %s %ds", VIA_NOM[reqViajeTipo], (int)(2.0f + 12.0f * guardado[5][1] + 0.5f)); }
    } else {                                                          // ESTADOS
      if (f[2]) { int n = (estadoAct + 7) & 7; cargarEstado(n); toast("ESTADO %s", ESTADOS[n].nom); }
      if (f[3]) { int n = (estadoAct + 1) & 7; cargarEstado(n); toast("ESTADO %s", ESTADOS[n].nom); }
      if (f[4]) { reqEvo = (reqEvo + 1) & 3; evoUlt = millis(); flashLevel = 0.8f; toast("EVOLUCION %s", EVO_NOM[reqEvo]); }
    }
  }
  if (reqEvo) {                                                       // EVOLUCION: la música no se detiene; cada tanto pasa sola a otro estado (y a veces de tónica)
    static const unsigned long INT_MS[4] = { 0, 120000UL, 60000UL, 25000UL };
    static unsigned long espera = 0;
    if (evoUlt == 0) evoUlt = t;
    if (espera == 0) espera = INT_MS[reqEvo & 3];
    if (t - evoUlt > espera) {
      evoUlt = t; espera = (unsigned long)(INT_MS[reqEvo & 3] * (0.7f + 0.6f * rnd01()));
      int n = (int)(xr() % 7); if (n >= estadoAct) n++;
      if ((xr() & 3) == 0) reqTon = (reqTon + 7) % 12;
      cargarEstado(n); toast("EVOLUCION: %s", ESTADOS[n].nom);
    }
  }
  if (capa != 3 || menuAbierto) reqBarrido = false;
  for (int i = 0; i < 5; i++) bL[i] = b[i];
  static const uint8_t POT_PIN[4] = { POT1, POT2, POT3, POT4 };
  static uint8_t scan = 0;
  int pi = scan; scan = (scan + 1) & 3;
  float v = readPot(POT_PIN[pi]);
  if (potUlt[pi] < 0.0f || fabsf(v - potUlt[pi]) > 0.004f) potUlt[pi] = v;
  leerPotCapa(pi, potUlt[pi]);
  pasoDeslizar();
#if DIAG && defined(FUZZ)
  { static unsigned long tf = 0; if (t - tf > 700) { tf = t; uint32_t r = xr();
      flagsA = (flagsA ^ (xr() & 0x7FFFFu)) & ~(uint32_t)(FL_CONG * ((r >> 3) & 1));
      reqEsc = xr() % NESC; reqTon = xr() % 12; reqEstilo = xr() & 3; runMask = (uint8_t)(xr() | 0x07) & 0x3F;
      if ((r & 3) == 0) reqRupN = reqRupN + 1; if ((r & 7) == 1) { reqViajeTipo = xr() & 3; reqViajeN = reqViajeN + 1; } if ((r & 15) == 2) reqElGenN = reqElGenN + 1; if ((r & 15) == 3) reqVaporN = reqVaporN + 1;
      for (int c = 0; c < NCAPAS; c++) for (int p = 0; p < 4; p++) par[c][p] = (c == 4 && p == 0) ? 0.8f : (float)(xr() % 1000) / 999.0f;
      if ((r & 31) == 5) cargarEstado(xr() & 7); } }
#endif
#if DIAG && defined(AUTOTEST)
  { static unsigned long ta = 0; static int paso = 0; if (t - ta > 14000 && t > 12000) { ta = t;
      if (paso == 0) reqRupN = reqRupN + 1; else { reqViajeTipo = (paso - 1) & 3; reqViajeN = reqViajeN + 1; }
      DBG_PRINTF("DIAG autotest: %s\n", paso == 0 ? "RUPTURA" : VIA_NOM[(paso - 1) & 3]); paso = (paso + 1) % 5; } }
#endif
  renderLEDs();
#if DIAG
  static unsigned long lastP = 0;
  if (t - lastP > 2000) {
    lastP = t;
    DBG_PRINTF("[%lus] DIAG render promedio %u us (%.0f %%)  maximo %u us (%.0f %%)  retrasos %u | pico %.2f sobre0.75 %u | esc %s ton %s acorde %d estado %d ctlmax %lu paginas-oled %lu mic %.4f curaciones %u mascara %u ligero %d micG %.1f recortes %u reduccion %.2f\n",
               (unsigned long)(millis() / 1000), (unsigned)g_usAvg, 100.0f * g_usAvg / BUF_US, (unsigned)g_usMax, 100.0f * g_usMax / BUF_US, (unsigned)g_xruns,
               dgPk, (unsigned)dgSobre, ESC_COR[g_esc], TON_NOM[g_ton], (int)g_acorde, (int)estadoAct, dgCtlMax, (unsigned long)dgPag, g_micEnv, (unsigned)g_heal, (unsigned)g_bad, (int)g_lite, g_micG, (unsigned)g_clip, g_gr);
    DBG_PRINTF("   ciclos/muestra: piano %u campanas %u dron %u indust %u sumas %u eco %u placa %u master %u | pre-bucle %u\n", ccS[0]/(ccBuf*128), ccS[1]/(ccBuf*128), ccS[2]/(ccBuf*128), ccS[3]/(ccBuf*128), ccS[4]/(ccBuf*128), ccS[5]/(ccBuf*128), ccS[6]/(ccBuf*128), 0u, ccS[9]/ccBuf);
    for (int k = 0; k < 10; k++) ccS[k] = 0; ccBuf = 0;
    g_usMax = 0; dgPk = 0; dgSobre = 0; dgCtlMax = 0;
  }
#endif
}

// ==============================================================================================================================================
// SETUP / TAREAS
// ==============================================================================================================================================
void audioTask(void *);
void controlTask(void *);
void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
#if DIAG
  Serial0.begin(115200);
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
  delay(300); DBG_PRINTF("BOOT motivo de reinicio=%d\n", (int)esp_reset_reason());
#endif
  pinMode(BTN1_PIN, INPUT_PULLUP); pinMode(BTN2_PIN, INPUT_PULLUP); pinMode(BTN3_PIN, INPUT_PULLUP);
  pinMode(BTN4_PIN, INPUT_PULLUP); pinMode(BTN5_PIN, INPUT_PULLUP);
  analogReadResolution(12); analogSetAttenuation(ADC_11db);
  for (int i = 0; i <= 1024; i++) {
    float th = 2.0f * (float)M_PI * (float)(i & 1023) / 1024.0f;
    SIN[i] = sinf(th);
    PIW[i] = sinf(th) + 0.62f * sinf(2 * th + 0.3f) + 0.36f * sinf(3 * th + 0.7f) + 0.22f * sinf(4 * th + 1.1f) + 0.13f * sinf(5 * th) + 0.08f * sinf(6 * th + 0.5f) + 0.05f * sinf(7 * th) + 0.03f * sinf(8 * th);
    DRW[i] = sinf(th) + 0.42f * sinf(2 * th) + 0.20f * sinf(3 * th) + 0.09f * sinf(4 * th);
  }
  { static const float H[6][8] = { { 1, .62f, .36f, .22f, .13f, .08f, .05f, .03f }, { 1, .45f, .18f, .07f, .03f, 0, 0, 0 }, { 1, .38f, .12f, .05f, 0, .04f, 0, 0 },
                                   { 1, 0, .12f, 0, .04f, 0, .02f, 0 }, { 1, .2f, .1f, .5f, .35f, .1f, .25f, 0 }, { 1, .7f, .5f, .3f, .15f, .08f, 0, 0 } };
    for (int t = 0; t < 6; t++) {
      float pk = 1e-9f;
      for (int i = 0; i < 1024; i++) { float th = 2.0f * (float)M_PI * (float)i / 1024.0f, v = 0; for (int h = 0; h < 8; h++) v += H[t][h] * sinf((float)(h + 1) * th + 0.3f * (float)h); PIT[t][i] = v; if (fabsf(v) > pk) pk = fabsf(v); }
      for (int i = 0; i < 1024; i++) PIT[t][i] /= pk; PIT[t][1024] = PIT[t][0];
    } }
  { float a = 1e-9f, c = 1e-9f; for (int i = 0; i < 1024; i++) { if (fabsf(PIW[i]) > a) a = fabsf(PIW[i]); if (fabsf(DRW[i]) > c) c = fabsf(DRW[i]); }
    for (int i = 0; i <= 1024; i++) { PIW[i] /= a; DRW[i] /= c; } }
  ecoL = (int16_t *)heap_caps_malloc(ECO_N * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ecoR = (int16_t *)heap_caps_malloc(ECO_N * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ecoL || !ecoR) { if (ecoL) heap_caps_free(ecoL); if (ecoR) heap_caps_free(ecoR);
    ecoL = (int16_t *)heap_caps_malloc(ECO_N * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); ecoR = (int16_t *)heap_caps_malloc(ECO_N * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  gBuf = (int16_t *)heap_caps_calloc(GB_N, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!gBuf) gBuf = (int16_t *)heap_caps_calloc(GB_N, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  resetAudio();
  rmtInit(LED_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  rmtInit(ONBOARD_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
  onboard[0] = CRGB::Black; ledsEscribir();
  for (int c = 0; c < NCAPAS; c++) for (int p = 0; p < 4; p++) par[c][p] = guardado[c][p];
  { const Estado &e = ESTADOS[0];                                     // arranque: CALMA, sin deslizar
    for (int q = 0; q < NV; q++) { guardado[capaDeIdx(q)][q & 3] = e.v[q]; par[capaDeIdx(q)][q & 3] = e.v[q]; }
    reqEsc = e.esc; reqEstilo = e.estilo; flagsA = e.fl & ~(uint32_t)FL_SW; estadoAct = 0; }
  sdMx = xSemaphoreCreateMutex();
  sdIniciar();                                                        // recupera la escena guardada (si hay tarjeta)
  esc = reqEsc % NESC; ton = reqTon % 12; grado = 0;
  dAct = 1; dronRetune(); dg[0] = 1.0f; dg[1] = 0.0f;
  elGenerar((flagsA >> EMOD_SH) & 3); elModoAct = (flagsA >> EMOD_SH) & 3;
  hK = coefDe(gDe(7500.0f), 0.8f);
#ifdef MIC_ON
  runMask |= 32; flagsA = (flagsA & ~(uint32_t)(3 << MUSO_SH)) | (1u << MUSO_SH) | MINF;
#endif
  pStepLen = 24000; cLen = SR * 60.0f / (30.0f + 90.0f * par[0][1]) * 16.0f;
  capaCtl = (capaCtl >= 0 && capaCtl < NCAPAS) ? capaCtl : 0;
  rngS ^= (uint32_t)micros() * 2654435761u; if (rngS == 0) rngS = 2463534242u;
  i2s_init();
  mic_init();
  xTaskCreatePinnedToCore(audioTask,   "audio",   8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 10240, NULL,  3, NULL, 0);
  capaT = millis();
#ifndef SIN_OLED
  xTaskCreatePinnedToCore(oledTask,    "oled",    8192, NULL,  1, NULL, OLED_CORE);
#endif
#ifndef SIN_SD
  xTaskCreatePinnedToCore(sdTask,      "sd",      10240, NULL,  1, NULL, 0);
#endif
}
void audioTask(void *)   { for (;;) renderBuffer(); }
void controlTask(void *) { for (;;) { pasoControl(); vTaskDelay(1); } }
void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
