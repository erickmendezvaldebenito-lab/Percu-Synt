// ==============================================================================================================================================
// PERCU-SYNTH — Cumbia Robot (máquina de arreglo manipulable, motor LIVIANO) — GC Lab Chile
// ==============================================================================================================================================
// HARDWARE
// ==============================================================================================================================================
// - ESP32-S3 + DAC PCM5102 (I2S: LCK 39, DIN 40, BCK 41) + 5 botones + 4 pots + IMU MPU6050 + 6 LEDs SMD WS2812
// ==============================================================================================================================================
// ARDUINO IDE
// ==============================================================================================================================================
// - Board: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · Monitor Serie a 115200 baudios
// - Librerías: FastLED (gestor de librerías de Arduino)
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// Una máquina para armar en vivo un tema de cumbia electrónica "robot" de 94 BPM y ~6 minutos, con la forma de un espectrograma de
// referencia (intro suave con un silbido agudo → arranque → pleno → quiebre oscuro → pleno con agudos abiertos → cierre).
// Esa forma es una INTERPRETACIÓN de una imagen (no puedo oír el tema): se mide como secciones con más o menos capas.
//
// POR QUÉ ESTE MOTOR ES DISTINTO (lección de los anteriores, que se oían con "interferencia"): se MIDIÓ en la placa lo que cuesta cada
// operación (sinf 1.0 µs, expf 1.2 µs, powf 2.9 µs, tanf 0.8 µs, floorf 0.4 µs, división 0.4 µs; el presupuesto es de 22.7 µs por
// muestra). Los motores anteriores gastaban casi todo en floorf / expf / divisiones por muestra, el audio llegaba tarde al DAC y éste
// rellenaba con ceros: eso es la interferencia. Aquí:
//   · osciladores de FASE ENTERA (uint32) con tabla de seno de 1024 puntos y armónicos exactos por multiplicación de la fase;
//   · cero floorf / expf / powf / tanf / divisiones por muestra (todo eso va por buffer o por golpe); decaimientos multiplicativos;
//   · filtros SVF cuyos coeficientes se calculan por buffer y se INTERPOLAN LINEALMENTE por muestra (sin saltos y sin recalcular);
//   · sólo se calcula lo que suena; reverb de placa tipo Dattorro; compresor + limitador + dither.
// Con DIAG=1 el firmware imprime por Serial0 (el puerto USB de esta placa es un puente UART) cuánta CPU gasta y si hubo retrasos.
//
// RITMO: capas euclídeas (k golpes en n pasos, con rotación) sobre una rejilla de 12 ticks por tiempo: caben semicorcheas y tresillos.
//   Tambora, güiro y clave latinos; bajo tipo tumbao; voz de robot (notas cortas de escala frigia de Fa#).
// ==============================================================================================================================================
// FUNCIONAMIENTO
// ==============================================================================================================================================
// - BTN1 → MUTAR ahora (toque): re-tira patrones y sonido de todas las capas
// - BTN2 → PERCUSIÓN GRAVE on/off (kick + tambora)
// - BTN3 → PERCUSIÓN AGUDA on/off (hats + güiro + clave + aire + silbido)
// - BTN4 → ARMONÍA on/off (bajo + órgano + voz de robot)
// - BTN5 → SECCIÓN siguiente (INTRO · ARRANQUE · PLENO · QUIEBRE · PLENO+ · CIERRE): cada una abre o cierra capas y mueve el filtro,
//          con transición suave de ~1.5 s. Los interruptores de arriba mandan por encima de la sección
//
// - POT1 → VOLUMEN
// - POT2 → MUTACIÓN: congelado ↔ muta solo cada pocos compases y muy lejos (el toque de BTN1 siempre muta)
// - POT3 → EFECTOS: cuánto eco y cuánta placa reciben las percusiones y la voz de robot
// - POT4 → FILTRO DJ: a la izquierda cierra, al centro está abierto, a la derecha adelgaza
// - IMU  → eje X: lanza el eco (a lo dub) · eje Y: abre la placa. La posición al ENCENDER es el "plano".
//
// LEDs: 0 = mutación · 1 = percusión grave · 2 = percusión aguda · 3 = armonía · 4 = sección (6 colores, late con el pulso) ·
//       5 = filtro DJ (azul cierra, magenta adelgaza); ROJO fijo = el audio llegó tarde. LED del módulo (GPIO 48): color = genes.
// ==============================================================================================================================================

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <Wire.h>
#include <FastLED.h>
#include <math.h>
#include <string.h>

// 1 = imprime por Serial0 (UART0 → puente USB) la carga de CPU y los retrasos cada 2 s. Al activarlo el BTN1 (GPIO 44 = RX) deja de funcionar.
#ifndef DIAG
#define DIAG 0
#endif

// ─── Tipos (arriba del todo para que el IDE de Arduino genere bien los prototipos) ───
struct Coef { float a1, a2, a3, k; };                // SVF TPT: coeficientes
struct SvfS { float ic1, ic2; };                      // SVF TPT: estado
struct Rampa { Coef cur, paso; };                     // coeficientes que se interpolan por muestra
struct Capa { uint8_t div, n, k, r; };                // patrón euclídeo
struct Seccion {
  const char *nombre;
  float kick, tom, hat, guiro, bajo, organo, robot, silbido, aire;     // ganancia de cada voz en la sección (0..1)
  float corte;                                        // corte del órgano (Hz)
  float fx;                                           // empuje de efectos
  uint8_t hue;
};

// ─── I2S PCM5102 ───────────────────────────────────────────
#define I2S_LCK   39
#define I2S_DIN   40
#define I2S_BCK   41
#define SAMPLE_RATE     44100
#define BUFFER_SAMPLES  128
const float SR     = (float)SAMPLE_RATE;
const float INV_SR = 1.0f / (float)SAMPLE_RATE;
const uint32_t BUF_US = (uint32_t)(1.0e6f * BUFFER_SAMPLES / SAMPLE_RATE);      // 2902 µs de presupuesto por buffer
const float INC_K = 4294967296.0f / (float)SAMPLE_RATE;                         // frecuencia (Hz) → incremento de fase de 32 bits

// ─── IMU MPU6050 ───────────────────────────────────────────
#define SDA_PIN     21
#define SCL_PIN     38
#define IMU_ADDR    0x68
const unsigned long IMU_READ_INTERVAL = 10;
const float IMU_FILTER_ALPHA = 0.08f;

// ─── LEDs ──────────────────────────────────────────────────
#define LED_PIN        46
#define NUM_LEDS        6
#define LED_BRIGHT     160
#define ONBOARD_PIN    48
#define ONBOARD_BRIGHT 40
const unsigned long LED_REFRESH_MS = 33;
CRGB leds[NUM_LEDS];
CRGB onboard[1];

// ─── Botones / pots ────────────────────────────────────────
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
// MÚSICA: 94 BPM, centro en Fa# (Fa#2 = 92.5 Hz), escala frigia
// ==============================================================================================================================================
const float BPM = 94.0f;
const float F0  = 92.5f;                                           // Fa#2
const int8_t FRIGIA[7] = { 0, 1, 3, 5, 7, 8, 10 };
const int8_t ESC_BAJO[8] = { 0, 0, 0, 1, 3, 7, 8, 12 };           // notas del bajo (semitonos sobre la fundamental del acorde)
const int8_t ROBOT_N[6]  = { 0, 3, 5, 7, 10, 12 };
const uint8_t PROG[4] = { 1, 1, 2, 1 };                            // grados: Fa#, Fa#, Sol, Fa# (cambia cada 4 compases)

// Secciones: una INTERPRETACIÓN del espectrograma de referencia
#define NSEC 6
const Seccion SEC[NSEC] = {
  //  nombre     kick  tom   hat   guiro bajo  org   robot silb  aire  corte  fx    hue
  { "INTRO",     0.00f,0.30f,0.30f,0.25f,0.00f,0.60f,0.00f,0.70f,0.55f, 900.f, 0.55f, 150 },
  { "ARRANQUE",  1.00f,0.60f,0.60f,0.55f,1.00f,0.80f,0.30f,0.00f,0.30f,1500.f, 0.30f,  40 },
  { "PLENO",     1.00f,0.85f,0.85f,0.95f,1.00f,1.00f,0.80f,0.00f,0.30f,2300.f, 0.35f,  10 },
  { "QUIEBRE",   0.00f,0.00f,0.12f,0.00f,0.45f,0.90f,0.00f,0.55f,0.80f, 520.f, 0.85f, 190 },
  { "PLENO+",    1.00f,0.90f,1.00f,1.00f,1.00f,1.00f,0.90f,0.20f,0.40f,3300.f, 0.40f,  25 },
  { "CIERRE",    1.00f,1.00f,0.90f,1.00f,0.70f,0.65f,1.00f,0.00f,0.30f,2600.f, 0.50f, 215 },
};

// ==============================================================================================================================================
// PEDIDOS ENTRE NÚCLEOS
// ==============================================================================================================================================
volatile uint32_t reqMutar   = 0;
volatile bool     reqGrave   = true;
volatile bool     reqAguda   = true;
volatile bool     reqArmonia = true;
volatile uint32_t reqSeccion = 0;       // contador; sección = contador % NSEC
volatile float    potVol     = 0.6f;
volatile float    potMut     = 0.30f;
volatile float    potFx      = 0.4f;
volatile float    potDj      = 0.5f;
volatile float    filtered_x = 0.0f;
volatile float    filtered_y = 0.0f;

volatile float    g_color    = 0.0f;
volatile float    g_mutFlash = 0.0f;
volatile float    g_kick     = 0.0f;
volatile float    g_hat      = 0.0f;
volatile float    g_arm      = 0.0f;
volatile float    g_pulso    = 0.0f;
volatile int      g_sec      = 0;
volatile unsigned long xrunAt = 0;
volatile uint32_t g_usAvg = 0, g_usMax = 0, g_xruns = 0;
#if DIAG
#include <xtensa/hal.h>
uint32_t cc[10] = {0,0,0,0,0,0,0,0,0,0}; uint32_t ccN = 0; uint32_t ccT;
#define MARCA(n) { uint32_t _t = xthal_get_ccount(); cc[n] += _t - ccT; ccT = _t; }
#else
#define MARCA(n)
#endif

// ==============================================================================================================================================
// UTILIDADES DSP (todas baratas por muestra)
// ==============================================================================================================================================
float SIN[1024 + 1];
inline float sinPh(uint32_t ph) {                                   // seno por tabla con interpolación; fase entera de 32 bits (los armónicos son ph·n)
  uint32_t i = ph >> 22;
  float f = (float)(ph & 0x3FFFFF) * (1.0f / 4194304.0f);
  float a = SIN[i];
  return a + (SIN[i + 1] - a) * f;
}
inline float tabPh(const float *t, uint32_t ph) {                  // lectura de una tabla de 1024 puntos (+1 de guarda) con interpolación
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
inline float limitador(float x) {
  float a = fabsf(x);
  if (a < 0.75f) return x;
  float t = (a - 0.75f) * 4.0f; if (t > 3.0f) t = 3.0f;
  float o = 0.75f + 0.25f * softClip(t);
  return x < 0.0f ? -o : o;
}

// ─── SVF TPT: coeficientes por buffer, interpolados por muestra ───
inline Coef coefDe(float g, float k) {
  Coef c; c.k = k;
  c.a1 = 1.0f / (1.0f + g * (g + k));
  c.a2 = g * c.a1;
  c.a3 = g * c.a2;
  return c;
}
inline float gDe(float fc) {                                        // g = tan(π·fc/SR) (sólo por buffer)
  if (fc > 0.49f * SR) fc = 0.49f * SR;
  if (fc < 10.0f) fc = 10.0f;
  return tanf((float)M_PI * fc * INV_SR);
}
inline void rampaHacia(Rampa &r, const Coef &t) {                   // fija el objetivo del buffer; el paso por muestra se reparte en BUFFER_SAMPLES
  const float inv = 1.0f / (float)BUFFER_SAMPLES;
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
inline float svfHP(SvfS &s, const Coef &c, float in) {
  float v3 = in - s.ic2;
  float v1 = c.a1 * s.ic1 + c.a2 * v3;
  float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
  s.ic1 = 2.0f * v1 - s.ic1;
  s.ic2 = 2.0f * v2 - s.ic2;
  return in - c.k * v1 - v2;
}
inline float svfBP(SvfS &s, const Coef &c, float in) {
  float v3 = in - s.ic2;
  float v1 = c.a1 * s.ic1 + c.a2 * v3;
  float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
  s.ic1 = 2.0f * v1 - s.ic1;
  s.ic2 = 2.0f * v2 - s.ic2;
  return c.k * v1;
}

// ─── Tablas de sierra limitada en banda (6 octavas, 1024 puntos) para el órgano; se generan al arrancar ───
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

// ==============================================================================================================================================
// RITMO — capas euclídeas sobre 12 ticks por tiempo
// ==============================================================================================================================================
enum { C_KICK, C_TOM, C_BAJO, C_HAT, C_GUIRO, C_CLAVE, C_ROBOT, NCAPAS };
Capa capa[NCAPAS];
const uint8_t COMBO[5][2] = { {6, 8}, {3, 16}, {4, 12}, {6, 6}, {3, 12} };       // (div, n): ciclos de 3 o 4 tiempos
inline bool golpe(const Capa &c, uint32_t paso) {
  int i = (int)((paso + c.r) % c.n);
  return ((i * c.k) % c.n) < c.k;
}
float tickLen = 2345.0f, tickAcc = 0.0f;
uint32_t tick = 0;

void cargarPatrones() {                                              // patrones de partida (hechos a mano)
  capa[C_KICK]  = {6, 8, 2, 0};      // x...x...  (tiempos 1 y 3)
  capa[C_TOM]   = {3, 16, 5, 2};     // tambora sincopada
  capa[C_BAJO]  = {6, 8, 5, 0};      // tumbao
  capa[C_HAT]   = {4, 12, 4, 1};
  capa[C_GUIRO] = {3, 16, 12, 0};    // güiro casi continuo a semicorcheas, con acentos
  capa[C_CLAVE] = {4, 12, 5, 3};
  capa[C_ROBOT] = {6, 8, 3, 3};
}

// Genes: sonido de las voces (se deslizan hacia su objetivo)
enum { G_KDEC, G_KCLK, G_HDEC, G_HFC, G_AIRE, G_TDEC, G_BLIP, NG_ };
float gen[NG_]  = { 0.15f, 0.25f, 0.040f, 7500.0f, 0.20f, 0.14f, 0.10f };
float genT[NG_] = { 0.15f, 0.25f, 0.040f, 7500.0f, 0.20f, 0.14f, 0.10f };
const float G_MIN[NG_]  = { 0.10f, 0.00f, 0.020f, 5500.0f, 0.00f, 0.08f, 0.05f };
const float G_MAX[NG_]  = { 0.24f, 0.50f, 0.085f, 9500.0f, 0.45f, 0.24f, 0.22f };
const float G_PASO[NG_] = { 0.08f, 0.30f, 0.040f, 2500.0f, 0.30f, 0.10f, 0.10f };
float mutFlash = 0.0f;
uint32_t mutBarras = 0;
bool mutPendiente = false;

// ==============================================================================================================================================
// VOCES (estado)
// ==============================================================================================================================================
// KICK: seno con barrido (decae por multiplicación) + golpe de ruido (diferencia de dos muestras = pasa-altos)
uint32_t kPh = 0; float kAmp = 0, kPit = 0, kClk = 0, kDec = 0.9999f; float kPrev = 0;
// TOM
uint32_t tPh = 0; float tAmp = 0, tPit = 0, tSlap = 0, tDec = 0.9998f, tGL = 1, tGR = 1; float tPrev = 0;
// BAJO: sub + 2º y 3º armónicos exactos
uint32_t bPh = 0, bInc = 0, bIncT = 0; float bEnv = 0; bool bGate = false; int bFin = -1;
// ÓRGANO: 3 notas de sierra limitada, mono
uint32_t oPh[3] = {0, 0x20000000u, 0x40000000u};
uint32_t oInc[3] = {0, 0, 0}, oIncT[3] = {0, 0, 0};
int oMip[3] = {1, 1, 1};
SvfS orgF; Rampa orgRamp;
bool gateK = true, gateT = true, gateH = true, gateG = true, gateB = true, gateR = true;   // la sección deja la voz en silencio: no se dispara
float BASSW[1025], ROBW[1025];                    // formas de onda horneadas (fundamental + armónicos exactos) para el bajo y el robot
float aireA = 0, aireB = 0, ecoLpL = 0, ecoLpR = 0, xAcc = 0, pLo = 0, pRo = 0, pLn = 0, pRn = 0, cmpGT = 1;
uint32_t pFase = 0; int djModo = 0; SvfS djL, djR; Rampa djRamp;
// HAT / GÜIRO / CLAVE / AIRE / SILBIDO
float hAmp = 0, hDec = 0.99f, hGL = 1, hGR = 1; SvfS hF; Coef hK;
float gAmp = 0, gDec = 0.99f, gGL = 1, gGR = 1; SvfS gF; Coef gK;
float cvAmp = 0, cvDec = 0.99f; uint32_t cvPh = 0;
uint32_t silPh = 0; const uint32_t silInc = (uint32_t)(3500.0f * (4294967296.0f / 44100.0f));
// ROBOT
uint32_t rPh = 0; uint32_t rInc = 0; float rAmp = 0, rDec = 0.999f, rPit = 0;
SvfS rF; Coef rK;
float duckSub = 0;
// ganancias de sección suavizadas y toggles
float gSec[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
float gTog[3] = {1, 1, 1};
float corteS = 1500.0f, fxS = 0.3f;

// ==============================================================================================================================================
// RACK — eco (corchea con puntillo = 9 ticks, ping-pong, oscuro) y placa de Dattorro
// ==============================================================================================================================================
#define ECO_N 21106                                                  // 9 ticks a 94 BPM
int16_t ecoL[ECO_N], ecoR[ECO_N];
int ecoW = 0;
float eHpL = 0, eHpR = 0; const float ECO_HP = 0.0128f;              // pasa-altos de ~90 Hz
float ecoFb = 0.45f;

float ia1[142], ia2[107], ia3[379], ia4[277];
int ii1 = 0, ii2 = 0, ii3 = 0, ii4 = 0;
float lAp1[672 + 40], lD1[4453], lAp2[1800], lD2[3720];
float rAp1[908 + 40], rD1[4217], rAp2[2656], rD2[3163];
int lAp1w = 0, lD1w = 0, lAp2w = 0, lD2w = 0, rAp1w = 0, rD1w = 0, rAp2w = 0, rD2w = 0;
float lpL = 0, lpR = 0, bwIn = 0, fbL = 0, fbR = 0;
uint32_t lfo1 = 0, lfo2 = 0x4CCCCCCCu;
const uint32_t lfo1Inc = (uint32_t)(0.55f * 4294967296.0f / 44100.0f), lfo2Inc = (uint32_t)(0.73f * 4294967296.0f / 44100.0f);

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
// ESTADO DEL AUDIO
// ==============================================================================================================================================
bool aGrave = true, aAguda = true, aArm = true;
uint32_t aSecSeen = 0, aMutSeen = 0;
int seccion = 0;
float dcXL = 0, dcYL = 0, dcXR = 0, dcYR = 0;
const float DC_R = 0.9986f;
float vVol = 0.6f, vFx = 0.4f, vThrow = 0.0f, vPlacaY = 0.0f;
float cmpEnv = 0, cmpGan = 1;
bool benchForce = false;

static i2s_chan_handle_t tx_chan;

void resetAudio() {
  memset(ia1, 0, sizeof(ia1)); memset(ia2, 0, sizeof(ia2)); memset(ia3, 0, sizeof(ia3)); memset(ia4, 0, sizeof(ia4));
  memset(lAp1, 0, sizeof(lAp1)); memset(lD1, 0, sizeof(lD1)); memset(lAp2, 0, sizeof(lAp2)); memset(lD2, 0, sizeof(lD2));
  memset(rAp1, 0, sizeof(rAp1)); memset(rD1, 0, sizeof(rD1)); memset(rAp2, 0, sizeof(rAp2)); memset(rD2, 0, sizeof(rD2));
  memset(ecoL, 0, sizeof(ecoL)); memset(ecoR, 0, sizeof(ecoR));
  lpL = lpR = bwIn = fbL = fbR = 0; eHpL = eHpR = 0;
  orgF = hF = gF = rF = djL = djR = SvfS{0, 0}; aireA = aireB = ecoLpL = ecoLpR = xAcc = pLo = pRo = pLn = pRn = 0; pFase = 0;
  kAmp = tAmp = hAmp = gAmp = cvAmp = rAmp = bEnv = 0; bGate = false; bFin = -1;
  dcXL = dcYL = dcXR = dcYR = 0; cmpEnv = 0; cmpGan = 1; duckSub = 0; ecoW = 0; tick = 0; tickAcc = 0;
}

// ─── armonía: acorde en curso → frecuencias del órgano y del bajo ───
IRAM_ATTR float notaHz(int grado, int extraSemi, int octavas) {
  int s = FRIGIA[(grado - 1) % 7] + extraSemi;
  return F0 * powf(2.0f, (float)s / 12.0f) * (float)(1 << octavas);
}
IRAM_ATTR void aplicarAcorde() {
  int d = PROG[(tick / 48 / 4) & 3];                                 // el acorde cambia cada 4 compases
  float f[3] = { notaHz(d, 0, 1), notaHz(d, 7, 1), notaHz(d, 12, 1) };   // fundamental, quinta, octava (desde Fa#3)
  for (int k = 0; k < 3; k++) { oIncT[k] = incF(f[k]); oMip[k] = mipDe(f[k]); }
}

// ─── mutación ───
IRAM_ATTR void mutarCapa(Capa &c, int cMin, int cMax, float prof, bool ancla) {
  if (rnd01() < 0.25f + 0.55f * prof) { int ix = cMin + (int)(rnd01() * (float)(cMax - cMin + 1)); if (ix > cMax) ix = cMax; c.div = COMBO[ix][0]; c.n = COMBO[ix][1]; }
  int kMax = c.n / 2; if (kMax < 3) kMax = 3;
  int kk = 2 + (int)(rnd01() * (float)(kMax - 1)); if (kk > kMax) kk = kMax;
  if (rnd01() < 0.5f + 0.4f * prof) c.k = kk;
  if (!ancla && rnd01() < 0.5f + 0.4f * prof) c.r = (uint8_t)((int)(rnd01() * (float)c.n) % c.n);
}
IRAM_ATTR void mutar(float prof) {
  for (int g = 0; g < NG_; g++) {
    float v = genT[g] + (rnd01() * 2.0f - 1.0f) * G_PASO[g] * prof;
    if (v < G_MIN[g]) v = G_MIN[g] + (G_MIN[g] - v) * 0.5f;
    if (v > G_MAX[g]) v = G_MAX[g] - (v - G_MAX[g]) * 0.5f;
    if (v < G_MIN[g]) v = G_MIN[g];
    if (v > G_MAX[g]) v = G_MAX[g];
    genT[g] = v;
  }
  mutarCapa(capa[C_KICK], 0, 2, prof, true);                         // el bombo siempre cae en el primer tiempo
  mutarCapa(capa[C_BAJO], 0, 3, prof, false);
  mutarCapa(capa[C_HAT], 1, 4, prof, false);
  mutarCapa(capa[C_TOM], 1, 2, prof, false);
  mutarCapa(capa[C_GUIRO], 1, 1, prof, false);
  mutarCapa(capa[C_CLAVE], 2, 4, prof, false);
  mutarCapa(capa[C_ROBOT], 0, 3, prof, false);
  mutFlash = 1.0f;
}

// ─── disparadores (aquí sí se pueden usar expf/powf: ocurren pocas veces por segundo) ───
IRAM_ATTR void dispararKick() {
  kAmp = 1.0f; kPit = 1.0f; kClk = gen[G_KCLK]; kPh = 0;
  kDec = expf(-1.0f / (gen[G_KDEC] * SR));
}
IRAM_ATTR void dispararTom(float v) {
  tAmp = v; tPit = 1.0f; tSlap = 0.7f * v; tPh = 0;
  tDec = expf(-1.0f / (gen[G_TDEC] * SR));
  float p = rnd01() - 0.5f; tGL = 1.0f - 0.36f * p; tGR = 1.0f + 0.36f * p;
}
IRAM_ATTR void dispararHat(float v, bool abierto) {
  hAmp = v; hDec = expf(-1.0f / (gen[G_HDEC] * (abierto ? 4.0f : 1.0f) * SR));
  float p = rnd01() - 0.5f; hGL = 1.0f - 0.72f * p; hGR = 1.0f + 0.72f * p;
}
IRAM_ATTR void dispararGuiro(float v, bool abajo) {
  gAmp = v; gDec = expf(-1.0f / ((abajo ? 0.060f : 0.032f) * SR));
  float p = rnd01() - 0.5f; gGL = 1.0f - 0.54f * p; gGR = 1.0f + 0.54f * p;
}
IRAM_ATTR void dispararClave(float v) { cvAmp = v; cvPh = 0; cvDec = expf(-1.0f / (0.018f * SR)); }
IRAM_ATTR void dispararRobot() {
  int ix = (int)(rnd01() * 5.99f);
  float f = F0 * powf(2.0f, (float)(ROBOT_N[ix]) / 12.0f) * 4.0f;    // dos octavas sobre el bajo
  rInc = incF(f); rAmp = 0.55f + 0.35f * rnd01(); rPit = 1.0f; rPh = 0;
  rDec = expf(-1.0f / (gen[G_BLIP] * SR));
}
IRAM_ATTR void dispararBajo(int paso) {
  int d = PROG[(tick / 48 / 4) & 3];
  int ix = (int)(rnd01() * 7.99f);
  float f = notaHz(d, ESC_BAJO[ix], 0);
  bIncT = incF(f);
  if (!bGate && bEnv < 0.02f) bInc = bIncT;
  bGate = true;
  const Capa &c = capa[C_BAJO];
  int dd = 1; for (; dd <= (int)c.n; dd++) if (golpe(c, (uint32_t)(paso + dd))) break;
  bFin = (int)((float)(dd * c.div) * tickLen * 0.92f);
}

IRAM_ATTR void pasoTick() {
  if (aGrave) {
    const Capa &k = capa[C_KICK];
    if (gateK && (tick % k.div) == 0 && golpe(k, tick / k.div)) dispararKick();
    const Capa &t = capa[C_TOM];
    if (gateT && (tick % t.div) == 0 && golpe(t, tick / t.div)) dispararTom(0.6f + 0.4f * rnd01());
  }
  if (aAguda) {
    const Capa &h = capa[C_HAT];
    if (gateH && (tick % h.div) == 0 && golpe(h, tick / h.div)) dispararHat(0.55f + 0.35f * rnd01(), (tick % 12) == 6 && rnd01() < 0.3f);
    const Capa &g = capa[C_GUIRO];
    if (gateG && (tick % g.div) == 0 && golpe(g, tick / g.div)) { uint32_t s = tick / g.div; dispararGuiro((s & 3) == 0 ? 0.9f : 0.5f, (s & 1) == 0); }
    const Capa &c = capa[C_CLAVE];
    if (gateG && (tick % c.div) == 0 && golpe(c, tick / c.div)) dispararClave(0.55f);
  }
  if (aArm) {
    const Capa &b = capa[C_BAJO];
    if (gateB && (tick % b.div) == 0 && golpe(b, tick / b.div)) dispararBajo((int)(tick / b.div));
    const Capa &r = capa[C_ROBOT];
    if (gateR && (tick % r.div) == 0 && golpe(r, tick / r.div)) dispararRobot();
  } else { bGate = false; bFin = -1; }
  if ((tick % 192) == 0) aplicarAcorde();                            // cada 4 compases
}

// ==============================================================================================================================================
// CONTROL (core 0)
// ==============================================================================================================================================
float readPot(uint8_t pin) { uint32_t s = 0; for (int i = 0; i < 4; i++) s += analogRead(pin); return (float)(s >> 2) / 4095.0f; }
float imuOffX = 0, imuOffY = 0, imuCalSx = 0, imuCalSy = 0; int imuCalN = 0; bool imuOk = false;
unsigned long lastIMURead = 0, lastIMURetry = 0; uint8_t imuFails = 0;
bool initIMU(bool warm) {
  Wire.begin(SDA_PIN, SCL_PIN); Wire.setClock(400000);
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x6B); Wire.write(0x00);
  bool ack = (Wire.endTransmission(true) == 0);
  delay(warm ? 8 : 60);
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x1C); Wire.write(0x00); Wire.endTransmission(true);
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x1A); Wire.write(0x03); Wire.endTransmission(true);
  if (!warm) delay(20);
  return ack;
}
void readIMU() {
  unsigned long t = millis();
  if (t - lastIMURead < IMU_READ_INTERVAL) return;
  lastIMURead = t;
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x3B); Wire.endTransmission(false); Wire.requestFrom(IMU_ADDR, 4, true);
  if (Wire.available() >= 4) {
    int16_t rx = (Wire.read() << 8) | Wire.read(); int16_t ry = (Wire.read() << 8) | Wire.read();
    float x = rx / 16384.0f, y = ry / 16384.0f;
    if (imuCalN < 40) { imuCalSx += x; imuCalSy += y; imuCalN++; if (imuCalN == 40) { imuOffX = imuCalSx / 40.0f; imuOffY = imuCalSy / 40.0f; } imuOk = true; imuFails = 0; return; }
    x -= imuOffX; y -= imuOffY;
    filtered_x = filtered_x * (1.0f - IMU_FILTER_ALPHA) + x * IMU_FILTER_ALPHA;
    filtered_y = filtered_y * (1.0f - IMU_FILTER_ALPHA) + y * IMU_FILTER_ALPHA;
    imuOk = true; imuFails = 0;
  } else if (++imuFails > 20) { imuFails = 0; imuOk = false; if (t - lastIMURetry > 3000) { lastIMURetry = t; imuOk = initIMU(true); } }
}

float flashLevel = 0.0f;
void renderLEDs() {
  static unsigned long lastFrame = 0;
  unsigned long t = millis();
  if (t - lastFrame < LED_REFRESH_MS) return;
  lastFrame = t;
  uint8_t hue = (uint8_t)(110.0f + g_color * 150.0f);
  leds[0] = CHSV(hue, 235, (uint8_t)(35 + g_mutFlash * 190.0f));
  leds[1] = CHSV(0,   255, reqGrave   ? (uint8_t)(25 + g_kick * 230.0f) : 3);
  leds[2] = CHSV(40,  255, reqAguda   ? (uint8_t)(25 + g_hat  * 230.0f) : 3);
  leds[3] = CHSV(150, 255, reqArmonia ? (uint8_t)(25 + g_arm  * 230.0f) : 3);
  leds[4] = CHSV(SEC[g_sec].hue, 255, (uint8_t)(30 + g_pulso * 200.0f));
  float dj = potDj;
  if (dj < 0.45f)      leds[5] = CHSV(160, 255, 40 + (uint8_t)((0.45f - dj) / 0.45f * 200.0f));
  else if (dj > 0.55f) leds[5] = CHSV(215, 255, 40 + (uint8_t)((dj - 0.55f) / 0.45f * 200.0f));
  else                 leds[5] = CRGB(30, 30, 30);
  if (xrunAt != 0 && millis() - xrunAt < 1500) leds[5] = CRGB(255, 0, 0);
  onboard[0] = CHSV(hue, 255, 20 + (uint8_t)(g_kick * 120.0f + g_arm * 100.0f));
  onboard[0].nscale8(ONBOARD_BRIGHT);
  if (flashLevel > 0.02f) { uint8_t w = (uint8_t)(flashLevel * 130.0f); for (int i = 0; i < NUM_LEDS; i++) leds[i] += CRGB(w, w, w); flashLevel *= 0.62f; }
  FastLED.show();
}

bool b1L = HIGH, b2L = HIGH, b3L = HIGH, b4L = HIGH, b5L = HIGH;
unsigned long b1T = 0, b2T = 0, b3T = 0, b4T = 0, b5T = 0;
float potUlt[4] = { -1, -1, -1, -1 };
void pasoControl() {
  unsigned long t = millis();
#if DIAG
  bool b1 = HIGH;                                                     // GPIO 44 es el RX de Serial0
#else
  bool b1 = digitalRead(BTN1_PIN);
#endif
  bool b2 = digitalRead(BTN2_PIN), b3 = digitalRead(BTN3_PIN), b4 = digitalRead(BTN4_PIN), b5 = digitalRead(BTN5_PIN);
  if (b1 == LOW && b1L == HIGH && t - b1T > DEBOUNCE_MS) { b1T = t; reqMutar = reqMutar + 1; flashLevel = 0.8f; }
  if (b2 == LOW && b2L == HIGH && t - b2T > DEBOUNCE_MS) { b2T = t; reqGrave   = !reqGrave;   flashLevel = 0.4f; }
  if (b3 == LOW && b3L == HIGH && t - b3T > DEBOUNCE_MS) { b3T = t; reqAguda   = !reqAguda;   flashLevel = 0.4f; }
  if (b4 == LOW && b4L == HIGH && t - b4T > DEBOUNCE_MS) { b4T = t; reqArmonia = !reqArmonia; flashLevel = 0.4f; }
  if (b5 == LOW && b5L == HIGH && t - b5T > DEBOUNCE_MS) { b5T = t; reqSeccion = reqSeccion + 1; flashLevel = 1.0f; }
  b1L = b1; b2L = b2; b3L = b3; b4L = b4; b5L = b5;
  readIMU();
  static const uint8_t POT_PIN[4] = { POT1, POT2, POT3, POT4 };
  static uint8_t scan = 0;
  int pi = scan; scan = (scan + 1) & 3;
  float v = readPot(POT_PIN[pi]);
  if (potUlt[pi] < 0.0f || fabsf(v - potUlt[pi]) > 0.004f) potUlt[pi] = v;
  float p = potUlt[pi];
  switch (pi) { case 0: potVol = p; break; case 1: potMut = p; break; case 2: potFx = p; break; case 3: potDj = p; break; }
  renderLEDs();
#if DIAG
  static unsigned long lastSecA = 0; if (t - lastSecA > 9000) { lastSecA = t; reqSeccion = reqSeccion + 1; }   // para medir todas las secciones sin pulsar nada
  static unsigned long lastP = 0;
  if (t - lastP > 2000) {
    lastP = t;
    Serial0.printf("DIAG sec=%s  render promedio %u us (%.0f %% del buffer)  maximo %u us (%.0f %%)  retrasos %u  gananciasSec[kick %.2f bajo %.2f organo %.2f]\n",
                   SEC[g_sec].nombre, (unsigned)g_usAvg, 100.0f * g_usAvg / BUF_US, (unsigned)g_usMax, 100.0f * g_usMax / BUF_US, (unsigned)g_xruns, gSec[0], gSec[4], gSec[5]);
    g_usMax = 0;
    if (ccN) { Serial0.printf("DIAG ciclos/muestra: reloj+suav %u | kick+tom %u | bajo %u | organo %u | hat/guiro/aire %u | robot %u | mezcla seca %u | eco %u | placa %u | dj+comp+salida %u\n",
      (unsigned)(cc[0]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[1]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[2]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[3]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[4]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[5]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[6]/(ccN*BUFFER_SAMPLES)),0u,(unsigned)(cc[7]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[8]/(ccN*BUFFER_SAMPLES))); for(int q=0;q<10;q++) cc[q]=0; ccN=0; }
  }
#endif
}

// ==============================================================================================================================================
// AUDIO
// ==============================================================================================================================================
IRAM_ATTR void renderCore(int16_t *buffer) {
  aGrave = reqGrave; aAguda = reqAguda; aArm = reqArmonia;
  if (benchForce || DIAG) { aGrave = aAguda = aArm = true; }               // en diagnóstico todo activo: se mide el peor caso
  uint32_t ss = reqSeccion;
  if (ss != aSecSeen) { aSecSeen = ss; seccion = (int)(ss % NSEC); }
  uint32_t mc = reqMutar;
  if (mc != aMutSeen) { aMutSeen = mc; mutPendiente = true; }
  for (int g = 0; g < NG_; g++) gen[g] += (genT[g] - gen[g]) * 0.0020f;
  mutFlash *= 0.93f;

  // ── objetivos por buffer (aquí sí valen tanf/powf: se pagan 1 vez cada 128 muestras) ──
  const Seccion &S = SEC[seccion];
  float secT[9] = { S.kick, S.tom, S.hat, S.guiro, S.bajo, S.organo, S.robot, S.silbido, S.aire };
  float togT[3] = { aGrave ? 1.0f : 0.0f, aAguda ? 1.0f : 0.0f, aArm ? 1.0f : 0.0f };
  for (int i = 0; i < 9; i++) gSec[i] += (secT[i] - gSec[i]) * 0.0030f;        // ~1.5 s por transición de sección
  for (int i = 0; i < 3; i++) gTog[i] += (togT[i] - gTog[i]) * 0.020f;
  corteS += (S.corte - corteS) * 0.0040f; fxS += (S.fx - fxS) * 0.0040f;
  const float gK_ = gSec[0] * gTog[0], gT_ = gSec[1] * gTog[0], gH_ = gSec[2] * gTog[1], gG_ = gSec[3] * gTog[1];
  const float gB_ = gSec[4] * gTog[2], gO_ = gSec[5] * gTog[2], gR_ = gSec[6] * gTog[2], gS_ = gSec[7] * gTog[1], gA_ = gSec[8] * gTog[1];
  gateK = gK_ > 0.01f; gateT = gT_ > 0.01f; gateH = gH_ > 0.01f; gateG = gG_ > 0.01f; gateB = gB_ > 0.01f; gateR = gR_ > 0.01f;

  float dj = potDj;                                                    // filtro DJ: un solo SVF por canal, en modo LP (izquierda) o HP (derecha)
  Coef djT;
  if (dj < 0.45f)      { float x = (0.45f - dj) / 0.45f; djModo = 0; djT = coefDe(gDe(20000.0f * powf(0.0040f, x)), 1.0f / (0.707f + 0.45f * x)); }
  else if (dj > 0.55f) { float x = (dj - 0.55f) / 0.45f; djModo = 1; djT = coefDe(gDe(20.0f * powf(180.0f, x)),    1.0f / (0.707f + 0.35f * x)); }
  else                 { djModo = 0; djT = coefDe(gDe(20000.0f), 1.4142f); }
  rampaHacia(djRamp, djT);
  rampaHacia(orgRamp, coefDe(gDe(corteS), 1.0f / 0.9f));

  float tx = filtered_x; if (tx > 1.0f) tx = 1.0f; if (tx < -1.0f) tx = -1.0f;
  float ty = filtered_y; if (ty > 1.0f) ty = 1.0f; if (ty < -1.0f) ty = -1.0f;
  float throwT = (fabsf(tx) > 0.12f) ? (fabsf(tx) - 0.12f) / 0.6f : 0.0f; if (throwT > 1.0f) throwT = 1.0f;
  float placaT = (fabsf(ty) > 0.12f) ? (fabsf(ty) - 0.12f) / 0.6f : 0.0f; if (placaT > 1.0f) placaT = 1.0f;
  float volT = potVol * potVol;
  float fxT = potFx * 0.7f + fxS * 0.5f; if (fxT > 1.0f) fxT = 1.0f;
  const float inv = 1.0f / (float)BUFFER_SAMPLES;
  const float dVol = (volT - vVol) * inv, dFx = (fxT - vFx) * inv, dTh = (throwT - vThrow) * inv, dPl = (placaT - vPlacaY) * inv;
  hK = coefDe(gDe(gen[G_HFC]), 1.0f);
  gK = coefDe(gDe(4200.0f), 1.0f / 0.9f);
  rK = coefDe(gDe(4200.0f), 1.4142f);
  const float aireNivel = gen[G_AIRE] * 0.060f * gA_;
  const float sendT = 0.42f + 0.22f * vFx + 0.22f * vThrow;
  const float decay = 0.52f + 0.12f * vFx + 0.06f * vPlacaY;               // la placa corre a 22 kHz: cada vuelta dura el doble, por eso el coeficiente es menor
  const float autoProf = 0.15f + 0.85f * potMut;
  float acumK = 0, acumH = 0, acumA = 0;
  for (int k = 0; k < 3; k++) { oInc[k] += (int32_t)(oIncT[k] - oInc[k]) / 32; }

  for (int i = 0; i < BUFFER_SAMPLES; i++) {
#if DIAG
    ccT = xthal_get_ccount();
#endif
    // ── reloj: 12 ticks por tiempo ──
    tickAcc += 1.0f;
    if (tickAcc >= tickLen) {
      tickAcc -= tickLen;
      if (mutPendiente) { mutPendiente = false; mutar(0.45f + 0.55f * potMut); }
      if ((tick % 48) == 0 && potMut > 0.05f) { mutBarras++; uint32_t cada = 3 + (uint32_t)(24.0f * (1.0f - potMut)); if (mutBarras >= cada) { mutBarras = 0; mutar(autoProf); } }
      pasoTick();
      tick++;
    }
    if (bFin >= 0 && --bFin < 0) bGate = false;
    rampaAvanza(djRamp); rampaAvanza(orgRamp);
    vVol += dVol; vFx += dFx; vThrow += dTh; vPlacaY += dPl;
    const float nz = ruido();                                           // un solo ruido por muestra para todas las voces de ruido
    MARCA(0)

    // ── KICK + TOM (sólo si suenan) ──
    float kick = 0, tom = 0;
    if (kAmp > 0.0004f) {
      kPh += incF(58.0f + 82.0f * kPit);
      float hp = nz - kPrev; kPrev = nz;
      kick = sinPh(kPh) * 0.78f * kAmp + (kClk > 0.01f ? hp * kClk * 0.18f * kAmp : 0.0f);
      kPit *= 0.9990f; kAmp *= kDec; kClk *= 0.9946f;
      kick *= gK_;
    } else kAmp = 0;
    duckSub += ((kAmp > 0.2f ? 1.0f : 0.0f) - duckSub) * 0.0020f;
    if (tAmp > 0.0004f) {
      tPh += incF(115.0f + 75.0f * tPit);
      float hp = nz - tPrev; tPrev = nz;
      tom = (sinPh(tPh) * tAmp * 0.55f + hp * tSlap * 0.12f) * gT_;
      tPit *= 0.9988f; tAmp *= tDec; tSlap *= 0.992f;
    } else tAmp = 0;
    MARCA(1)

    // ── BAJO: una sola tabla con la fundamental, el 2º armónico (−8 dB) y el 3º (−12 dB) ya mezclados ──
    float bajo = 0;
    if (bGate) bEnv += (1.0f - bEnv) * 0.0042f; else { bEnv *= 0.9990f; if (bEnv < 0.0005f) bEnv = 0; }
    if (bEnv > 0) {
      bInc += (int32_t)(bIncT - bInc) >> 6;
      bPh += bInc;
      bajo = tabPh(BASSW, bPh) * 0.60f * bEnv * (1.0f - 0.55f * duckSub) * gB_;
    }
    MARCA(2)

    // ── ÓRGANO: 3 notas de sierra limitada en banda, mono, un solo filtro ──
    float org = 0;
    if (gO_ > 0.002f) {
      float sum = 0;
      for (int k = 0; k < 3; k++) { oPh[k] += oInc[k]; sum += leeTabla(tabSaw[oMip[k]], oPh[k]); }
      org = svfLP(orgF, orgRamp.cur, sum * 0.16f) * gO_;
    }
    MARCA(3)

    // ── HAT / GÜIRO / CLAVE / AIRE / SILBIDO ──
    float hat = 0, gui = 0, cvv = 0, aire = 0, sil = 0;
    if (hAmp > 0.0004f) { hat = svfBP(hF, hK, nz) * hAmp * 0.24f * gH_; hAmp *= hDec; } else hAmp = 0;
    if (gAmp > 0.0004f) { gui = svfBP(gF, gK, nz) * gAmp * 0.22f * gG_; gAmp *= gDec; } else gAmp = 0;
    if (cvAmp > 0.0004f) { cvPh += incF(2500.0f); cvv = sinPh(cvPh) * cvAmp * 0.16f * gG_; cvAmp *= cvDec; } else cvAmp = 0;
    if (aireNivel > 0.0004f) { aireA += 0.35f * (nz - aireA); aireB += 0.06f * (aireA - aireB); aire = (aireA - aireB) * aireNivel; }   // aire: banda ancha con dos polos baratos
    if (gS_ > 0.002f) { silPh += silInc; sil = sinPh(silPh) * 0.045f * gS_; }
    MARCA(4)

    // ── VOZ DE ROBOT: una sola tabla (fundamental + 3º + 5º armónicos) ──
    float rob = 0;
    if (rAmp > 0.0004f) {
      rPh += rInc + (uint32_t)((float)rInc * 0.06f * rPit);
      rob = svfLP(rF, rK, tabPh(ROBW, rPh)) * rAmp * 0.20f * gR_;
      rPit *= 0.9990f; rAmp *= rDec;
    } else rAmp = 0;
    MARCA(5)

    // ── BUS SECO y ECO ping-pong (corchea con puntillo; filtros de un polo) ──
    float centro = kick * 0.95f + bajo + tom * 0.7f + rob * 0.6f + org;
    float dryL = centro + hat * hGL + gui * gGL + cvv * 0.8f + aire + tom * 0.3f * tGL + sil;
    float dryR = centro + hat * hGR + gui * gGR + cvv * 1.2f + aire * 0.9f + tom * 0.3f * tGR + sil;
    float oL = ecoL[ecoW] * (1.0f / 32768.0f), oR = ecoR[ecoW] * (1.0f / 32768.0f);
    ecoLpL += 0.26f * (oL - ecoLpL); ecoLpR += 0.26f * (oR - ecoLpR);
    eHpL += ECO_HP * (ecoLpL - eHpL); float lzL = ecoLpL - eHpL;
    eHpR += ECO_HP * (ecoLpR - eHpR); float lzR = ecoLpR - eHpR;
    ecoFb += (sendT - ecoFb) * 0.0006f;
    const float ecoSend = 0.10f + 0.55f * vFx + 0.35f * vThrow;
    float sL = hat * hGL * 0.9f + gui * gGL * 0.6f + cvv + rob + tom * tGL * 0.7f + kick * 0.15f;
    float sR = hat * hGR * 0.9f + gui * gGR * 0.6f + cvv + rob + tom * tGR * 0.7f + kick * 0.15f;
    float wL = sL * ecoSend + lzR * ecoFb, wR = sR * ecoSend + lzL * ecoFb;
    if (wL > 0.8f || wL < -0.8f) wL = limitador(wL);
    if (wR > 0.8f || wR < -0.8f) wR = limitador(wR);
    ecoL[ecoW] = (int16_t)(wL * 30000.0f); ecoR[ecoW] = (int16_t)(wR * 30000.0f);
    if (++ecoW >= ECO_N) ecoW = 0;
    float retEL = lzL * (0.45f + 0.55f * vFx), retER = lzR * (0.45f + 0.55f * vFx);
    MARCA(6)

    // ── PLACA (Dattorro) a 22.05 kHz: se calcula una muestra de cada dos y se interpola ──
    xAcc += ((sL + sR) * 0.5f + (retEL + retER) * 0.35f + org * 0.10f + sil * 0.6f);
    if (pFase == 0) { pFase = 1; }
    else {
      pFase = 0;
      float envio = 0.015f + 0.18f * vFx + 0.30f * vPlacaY;
      float x = xAcc * 0.5f * envio * 3.0f; xAcc = 0;
      bwIn += 0.80f * (x - bwIn);
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
    float pL = 0.5f * (pLo + pLn), pR = 0.5f * (pRo + pRn);
    const float colaG = 0.22f + 0.55f * vFx + 0.50f * vPlacaY;
    MARCA(7)

    // ── MEZCLA → FILTRO DJ (un SVF por canal) → DC → compresor → volumen → limitador → dither ──
    float mL = dryL + retEL * (0.5f + vThrow) + pL * colaG;
    float mR = dryR + retER * (0.5f + vThrow) + pR * colaG;
    if (djModo == 0) { mL = svfLP(djL, djRamp.cur, mL); mR = svfLP(djR, djRamp.cur, mR); }
    else             { mL = svfHP(djL, djRamp.cur, mL); mR = svfHP(djR, djRamp.cur, mR); }
    float yL = mL - dcXL + DC_R * dcYL; dcXL = mL; dcYL = yL;
    float yR = mR - dcXR + DC_R * dcYR; dcXR = mR; dcYR = yR;
    float pk = fabsf(yL) > fabsf(yR) ? fabsf(yL) : fabsf(yR);
    cmpEnv += (pk - cmpEnv) * (pk > cmpEnv ? 0.0110f : 0.00016f);
    if ((i & 7) == 0) {                                                // el cálculo de la ganancia (lleva una división) sólo cada 8 muestras
      cmpGT = 1.0f;
      if (cmpEnv > 0.34f) cmpGT = (0.34f + (cmpEnv - 0.34f) * (1.0f / 3.5f)) / cmpEnv;
    }
    cmpGan += (cmpGT - cmpGan) * (cmpGT < cmpGan ? 0.0110f : 0.00016f);
    const float gain = cmpGan * 1.69f * vVol;
    float oL2 = limitador(yL * gain), oR2 = limitador(yR * gain);
    uint32_t r1 = xr(), r2 = xr();                                      // dither triangular ±1 LSB: dos uniformes de 16 bits por canal con un solo número aleatorio
    float dL = (float)((int)(r1 & 0xFFFF) - (int)(r1 >> 16)) * (1.0f / 65536.0f);
    float dR = (float)((int)(r2 & 0xFFFF) - (int)(r2 >> 16)) * (1.0f / 65536.0f);
    buffer[i * 2]     = (int16_t)((int)(oL2 * 32000.0f + dL + 32768.5f) - 32768);
    buffer[i * 2 + 1] = (int16_t)((int)(oR2 * 32000.0f + dR + 32768.5f) - 32768);
    MARCA(8)

    acumK += kAmp; acumH += hAmp + gAmp; acumA += bEnv + gO_ * 0.3f;
  }
#if DIAG
  ccN++;
#endif
  float kk = acumK * (1.0f / BUFFER_SAMPLES); if (kk > 1) kk = 1;
  float hh = acumH * (1.0f / BUFFER_SAMPLES) * 1.4f; if (hh > 1) hh = 1;
  float aa = acumA * (1.0f / BUFFER_SAMPLES); if (aa > 1) aa = 1;
  g_kick = kk; g_hat = hh; g_arm = aa; g_mutFlash = mutFlash; g_sec = seccion;
  uint32_t bp = tick % 12; g_pulso = (bp < 3) ? 1.0f - (float)bp * 0.3f : 0.0f;
  float c = 0.30f * ((gen[G_KDEC] - 0.10f) / 0.14f) + 0.25f * ((gen[G_HFC] - 5500.0f) / 4000.0f) + 0.25f * (gen[G_AIRE] / 0.45f) + 0.20f * (gen[G_BLIP] - 0.05f) / 0.17f;
  g_color = c < 0 ? 0 : (c > 1 ? 1 : c);
}

void renderBuffer() {
  int16_t buffer[BUFFER_SAMPLES * 2];
  uint32_t t0 = micros();
  renderCore(buffer);
  uint32_t dt = micros() - t0;
  g_usAvg = (g_usAvg * 15 + dt) / 16;
  if (dt > g_usMax) g_usMax = dt;
  if (dt > BUF_US - 150) { xrunAt = millis(); g_xruns++; }
  size_t written;
  i2s_channel_write(tx_chan, buffer, sizeof(buffer), &written, portMAX_DELAY);
}

// ==============================================================================================================================================
// SETUP / TAREAS
// ==============================================================================================================================================
void i2s_init() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true; chan_cfg.dma_desc_num = 6; chan_cfg.dma_frame_num = 240;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)I2S_BCK, .ws = (gpio_num_t)I2S_LCK, .dout = (gpio_num_t)I2S_DIN, .din = I2S_GPIO_UNUSED, .invert_flags = { false, false, false } },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void audioTask(void *);
void controlTask(void *);

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
#if DIAG
  Serial0.begin(115200);
#endif
  pinMode(BTN1_PIN, INPUT_PULLUP); pinMode(BTN2_PIN, INPUT_PULLUP); pinMode(BTN3_PIN, INPUT_PULLUP);
  pinMode(BTN4_PIN, INPUT_PULLUP); pinMode(BTN5_PIN, INPUT_PULLUP);
  analogReadResolution(12); analogSetAttenuation(ADC_11db);
  for (int i = 0; i <= 1024; i++) SIN[i] = sinf(2.0f * (float)M_PI * (float)(i & 1023) / 1024.0f);
  construirTablas();
  for (int i = 0; i <= 1024; i++) {                                    // formas de onda horneadas: una lectura de tabla en vez de tres senos
    float th = 2.0f * (float)M_PI * (float)(i & 1023) / 1024.0f;
    BASSW[i] = (sinf(th) + 0.42f * sinf(2.0f * th) + 0.25f * sinf(3.0f * th)) * 0.62f;
    ROBW[i]  = (sinf(th) + 0.30f * sinf(3.0f * th) + 0.12f * sinf(5.0f * th)) * 0.80f;
  }
  tickLen = SR * 60.0f / (BPM * 12.0f);
  cargarPatrones();
  resetAudio();
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812, ONBOARD_PIN, GRB>(onboard, 1);
  FastLED.setBrightness(LED_BRIGHT); FastLED.clear(); FastLED.show();
  imuOk = initIMU(false); delay(30); readIMU();
  aplicarAcorde();
  for (int k = 0; k < 3; k++) oInc[k] = oIncT[k];
  bInc = bIncT = incF(F0);
  orgRamp.cur = coefDe(gDe(1500.0f), 1.0f / 0.9f); orgRamp.paso = {0, 0, 0, 0};
  djRamp.cur = coefDe(gDe(20000.0f), 1.4142f); djRamp.paso = {0, 0, 0, 0};
  i2s_init();
  xTaskCreatePinnedToCore(audioTask,   "audio",   8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 6144, NULL,  3, NULL, 0);
}
void audioTask(void *)   { for (;;) renderBuffer(); }
void controlTask(void *) { for (;;) { pasoControl(); vTaskDelay(1); } }
void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
