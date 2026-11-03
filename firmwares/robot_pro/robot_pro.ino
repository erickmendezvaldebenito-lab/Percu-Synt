// ==============================================================================================================================================
// PERCU-SYNTH — Robot Pro (cumbia electrónica "robot": arreglo + diseño en DOS CAPAS de control) — GC Lab Chile
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
// Un instrumento de ~6 minutos de cumbia robot (86–116 BPM, centro en Fa# frigio) pensado para ARMAR y TOCAR un tema entero.
// BTN1 cambia de CAPA. En cada capa los botones 2–5 y los pots 2–4 hacen otra cosa; POT1 es siempre el volumen. Todo lo que
// enciendas sigue sonando al cambiar de capa: las capas cambian qué TOCAS, no qué se OYE.
//
//   CAPA A · ARREGLO   (la forma del tema)   SECCIÓN · MUTAR · RELLENO · CAPAS  |  TEMPO · DENSIDAD · GROOVE
//   CAPA B · SONIDO    (los gestos en vivo)  REPETIR · LANZAR ECO · INFINITO · RISER  |  FILTRO DJ · EFECTOS · MUTACIÓN
//
// ELEMENTOS (todos síntesis, nada de samples):  kick · tambora · caja · hats · güiro · clave · bajo tumbao (2º y 3º armónicos
// exactos) · órgano de sierra limitada en banda (quinta + octava) · voz de robot con riff melódico mutable · silbido de 3.5 kHz ·
// aire · riser con IMPACTO · eco ping-pong a la corchea · placa de reverb tipo Dattorro · compresor · limitador · dither.
// RITMO: capas euclídeas (k golpes en n pasos, con rotación) sobre una rejilla de 12 ticks por tiempo: semicorcheas y tresillos
// conviven. GROOVE da swing y humanización. El tema MODULA: el QUIEBRE baja un tono y el PLENO+ sube un tono.
//
// MOTOR LIVIANO (medido en la placa, no estimado): fase entera, tablas, cero floorf/expf/powf/tanf/divisiones por muestra, código
// caliente en IRAM, placa de reverb a media frecuencia. Con -DDIAG=1 imprime por Serial0 (el USB de esta placa es un puente UART)
// la carga de CPU de cada sección y los retrasos: la ÚNICA forma honesta de saber si el audio llega a tiempo.
// ==============================================================================================================================================
// FUNCIONAMIENTO
// ==============================================================================================================================================
// - POT1 → VOLUMEN (siempre)
// - BTN1 → CAPA siguiente (A ↔ B). LED 0: naranja = A · cian = B
//
// CAPA A · ARREGLO
//   BTN2 → SECCIÓN siguiente (INTRO · ARRANQUE · PLENO · QUIEBRE · PLENO+ · CIERRE): abre/cierra elementos, mueve el filtro y modula
//   BTN3 → MUTAR: re-tira patrones, riff y sonido de todas las voces
//   BTN4 → RELLENO: hasta el final del compás la batería hace un redoble de tambora y caja con crescendo y un riser; al caer, IMPACTO
//   BTN5 → CAPAS (cada toque): TODO · sin grave · sin aguda · sin armonía · sólo grave · sólo aguda · sólo armonía
//   POT2 → TEMPO: 86 · 90 · 94 · 98 · 104 · 110 · 116 BPM (7 zonas; el eco las sigue sin cambiar de tono)
//   POT3 → DENSIDAD: menos golpes ↔ más golpes en tambora, caja, hats, güiro, clave y robot
//   POT4 → GROOVE: recto ↔ swing de tresillo + humanización de velocidades
//
// CAPA B · SONIDO
//   BTN2 → REPETIR (mantener): repite una rebanada de la mezcla, cada vez más corta (1/16 → 1/32 → 1/64)
//   BTN3 → LANZAR ECO (mantener): todo entra al eco y la realimentación sube (a lo dub)
//   BTN4 → INFINITO (mantener): congela la cola de la placa
//   BTN5 → RISER (mantener = sube; SOLTAR = IMPACTO)
//   POT2 → FILTRO DJ: izquierda cierra, centro abierto, derecha adelgaza
//   POT3 → EFECTOS: cuánto eco y cuánta placa reciben las percusiones, el robot y el silbido
//   POT4 → MUTACIÓN: apagada ↔ muta sola cada pocos compases y muy lejos
//   IMU  → eje X: lanza el eco · eje Y: abre la placa (siempre activo). La posición al ENCENDER es el "plano".
//
// Los pots de cada capa son de RECOGIDA: al cambiar de capa un pot no actúa hasta pasar por el valor que tenía; LED 5 ámbar mientras tanto.
// LEDs: 0 capa · 1–4 según la capa (A: grave, aguda, armonía, sección · B: repetir, eco, infinito, riser) · 5 recogida / DJ / ROJO = audio tarde.
// ==============================================================================================================================================

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <Wire.h>
#include <FastLED.h>
#include <math.h>
#include <string.h>

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
  float kick, tom, caja, hat, guiro, bajo, organo, robot, silbido, aire;   // ganancia de cada voz en la sección (0..1)
  float corte;                                        // corte del órgano (Hz)
  float fx;                                           // empuje de efectos
  int8_t trans;                                       // transposición de la sección (semitonos)
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
// MÚSICA: centro en Fa# (Fa#2 = 92.5 Hz), escala frigia
// ==============================================================================================================================================
const int NTEMPO = 7;
const float TEMPOS[NTEMPO] = { 86, 90, 94, 98, 104, 110, 116 };
const float F0  = 92.5f;                                           // Fa#2
const int8_t FRIGIA[7] = { 0, 1, 3, 5, 7, 8, 10 };
const int8_t ESC_BAJO[8] = { 0, 0, 0, 1, 3, 7, 8, 12 };           // notas del bajo (semitonos sobre la fundamental del acorde)
const int8_t ROBOT_N[6]  = { 0, 3, 5, 7, 10, 12 };
const uint8_t PROG[4] = { 1, 1, 2, 1 };                            // grados: Fa#, Fa#, Sol, Fa# (cambia cada 4 compases)

// Secciones: una INTERPRETACIÓN del espectrograma de referencia (el tema real no se puede oír desde aquí)
#define NSEC 6
const Seccion SEC[NSEC] = {
  //  nombre     kick  tom   caja  hat   guiro bajo  org   robot silb  aire  corte   fx     trans hue
  { "INTRO",     0.00f,0.30f,0.00f,0.30f,0.25f,0.00f,0.60f,0.00f,0.70f,0.55f, 900.f, 0.55f,  0,  150 },
  { "ARRANQUE",  1.00f,0.60f,0.50f,0.60f,0.55f,1.00f,0.80f,0.30f,0.00f,0.30f,1500.f, 0.30f,  0,   40 },
  { "PLENO",     1.00f,0.85f,0.80f,0.85f,0.95f,1.00f,1.00f,0.80f,0.00f,0.30f,2300.f, 0.35f,  0,   10 },
  { "QUIEBRE",   0.00f,0.00f,0.00f,0.12f,0.00f,0.45f,0.90f,0.00f,0.55f,0.80f, 520.f, 0.85f, -2,  190 },
  { "PLENO+",    1.00f,0.90f,0.90f,1.00f,1.00f,1.00f,1.00f,0.90f,0.20f,0.40f,3300.f, 0.40f,  2,   25 },
  { "CIERRE",    1.00f,1.00f,0.90f,0.90f,1.00f,0.70f,0.65f,1.00f,0.00f,0.30f,2600.f, 0.50f,  0,  215 },
};
const int NMASK = 7;
const char *MASK_NOM[NMASK] = { "TODO", "SIN GRAVE", "SIN AGUDA", "SIN ARMONIA", "SOLO GRAVE", "SOLO AGUDA", "SOLO ARMONIA" };

// ==============================================================================================================================================
// PEDIDOS ENTRE NÚCLEOS (la tarea de control escribe, la de audio aplica en el borde de un buffer)
// ==============================================================================================================================================
volatile int      capaCtl    = 0;       // 0 = A · 1 = B
volatile uint32_t reqSeccion = 0;       // A · BTN2
volatile uint32_t reqMutar   = 0;       // A · BTN3
volatile uint32_t reqFill    = 0;       // A · BTN4
volatile uint32_t reqMasc    = 0;       // A · BTN5
volatile bool     reqRepeat  = false;   // B · BTN2 (mantenido)
volatile bool     reqThrow   = false;   // B · BTN3 (mantenido)
volatile bool     reqFreeze  = false;   // B · BTN4 (mantenido)
volatile bool     reqRiser   = false;   // B · BTN5 (mantenido)
volatile float    potVol     = 0.6f;
volatile float    potTempo   = 2.5f / 7.0f;   // A · POT2
volatile float    potDens    = 0.5f;          // A · POT3
volatile float    potGroove  = 0.2f;          // A · POT4
volatile float    potDj      = 0.5f;          // B · POT2
volatile float    potFx      = 0.35f;         // B · POT3
volatile float    potMut     = 0.0f;          // B · POT4
volatile bool     g_sinRecoger = false;
volatile float    filtered_x = 0.0f;
volatile float    filtered_y = 0.0f;

volatile float    g_color    = 0.0f;
volatile float    g_mutFlash = 0.0f;
volatile float    g_kick     = 0.0f;
volatile float    g_hat      = 0.0f;
volatile float    g_arm      = 0.0f;
volatile float    g_pulso    = 0.0f;
volatile float    g_ris      = 0.0f;
volatile bool     g_rep = false, g_thr = false, g_frz = false, g_fill = false;
volatile int      g_sec      = 0;
volatile int      g_mask     = 0;
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
enum { C_KICK, C_TOM, C_CAJA, C_BAJO, C_HAT, C_GUIRO, C_CLAVE, C_ROBOT, NCAPAS };
enum { GR_GRAVE, GR_AGUDA, GR_ARM };
const uint8_t GRUPO[NCAPAS]   = { GR_GRAVE, GR_GRAVE, GR_GRAVE, GR_ARM, GR_AGUDA, GR_AGUDA, GR_AGUDA, GR_ARM };
const uint8_t GSEC_IX[NCAPAS] = { 0, 1, 2, 5, 3, 4, 4, 7 };       // ganancia de sección que manda en cada capa
const bool    USA_DENS[NCAPAS] = { false, true, true, false, true, true, true, true };    // cuáles escala la DENSIDAD
Capa capa[NCAPAS];
uint8_t kEff[NCAPAS];
bool actC[NCAPAS];
uint32_t pendT[NCAPAS]; uint32_t pendS[NCAPAS];
const uint8_t COMBO[5][2] = { {6, 8}, {3, 16}, {4, 12}, {6, 6}, {3, 12} };       // (div, n): ciclos de 3 o 4 tiempos
inline bool golpeEff(int l, uint32_t paso) {
  const Capa &c = capa[l];
  int i = (int)((paso + c.r) % c.n);
  return ((i * kEff[l]) % c.n) < kEff[l];
}
float tickLen = 2345.0f, tickAcc = 0.0f;
uint32_t tick = 0;

void cargarPatrones() {                                              // patrones de partida (hechos a mano)
  capa[C_KICK]  = {6, 8, 2, 0};      // x...x...  (tiempos 1 y 3)
  capa[C_TOM]   = {3, 16, 5, 2};     // tambora sincopada
  capa[C_CAJA]  = {6, 8, 2, 2};      // contratiempos: tiempos 2 y 4
  capa[C_BAJO]  = {6, 8, 5, 0};      // tumbao
  capa[C_HAT]   = {4, 12, 4, 1};
  capa[C_GUIRO] = {3, 16, 12, 0};    // güiro casi continuo a semicorcheas, con acentos
  capa[C_CLAVE] = {4, 12, 5, 3};
  capa[C_ROBOT] = {6, 8, 3, 3};
  for (int l = 0; l < NCAPAS; l++) { kEff[l] = capa[l].k; pendT[l] = 0xFFFFFFFFu; pendS[l] = 0; }
}

// Genes: sonido de las voces (se deslizan hacia su objetivo)
enum { G_KDEC, G_KCLK, G_HDEC, G_HFC, G_AIRE, G_TDEC, G_BLIP, G_CDEC, NG_ };
float gen[NG_]  = { 0.15f, 0.25f, 0.040f, 7500.0f, 0.20f, 0.14f, 0.10f, 0.11f };
float genT[NG_] = { 0.15f, 0.25f, 0.040f, 7500.0f, 0.20f, 0.14f, 0.10f, 0.11f };
const float G_MIN[NG_]  = { 0.10f, 0.00f, 0.020f, 5500.0f, 0.00f, 0.08f, 0.05f, 0.07f };
const float G_MAX[NG_]  = { 0.24f, 0.50f, 0.085f, 9500.0f, 0.45f, 0.24f, 0.22f, 0.20f };
const float G_PASO[NG_] = { 0.08f, 0.30f, 0.040f, 2500.0f, 0.30f, 0.10f, 0.10f, 0.08f };
float mutFlash = 0.0f;
uint32_t mutBarras = 0;
bool mutPendiente = false;
uint8_t riff[8] = { 0, 2, 3, 2, 4, 3, 1, 5 };                        // riff del robot: índices en ROBOT_N (mutable)
uint8_t riffPos = 0;

// ==============================================================================================================================================
// VOCES (estado)
// ==============================================================================================================================================
// KICK: seno con barrido (decae por multiplicación) + golpe de ruido (diferencia de dos muestras = pasa-altos)
uint32_t kPh = 0; float kAmp = 0, kPit = 0, kClk = 0, kDec = 0.9999f; float kPrev = 0;
// TOM
uint32_t tPh = 0; float tAmp = 0, tPit = 0, tSlap = 0, tDec = 0.9998f, tGL = 1, tGR = 1; float tPrev = 0;
// CAJA: tono corto de ~190 Hz + ruido en banda
uint32_t cjPh = 0; float cjAmp = 0, cjDec = 0.999f, cjTono = 0, cjGL = 1, cjGR = 1; SvfS cjF; Coef cjK;
// BAJO: una tabla horneada con la fundamental, el 2º armónico (−8 dB) y el 3º (−12 dB)
uint32_t bPh = 0, bInc = 0, bIncT = 0; float bEnv = 0; bool bGate = false; int bFin = -1;
// ÓRGANO: 3 notas de sierra limitada, mono
uint32_t oPh[3] = {0, 0x20000000u, 0x40000000u};
uint32_t oInc[3] = {0, 0, 0}, oIncT[3] = {0, 0, 0};
int oMip[3] = {1, 1, 1};
SvfS orgF; Rampa orgRamp;
float BASSW[1025], ROBW[1025];                    // formas de onda horneadas
float aireA = 0, aireB = 0, ecoLpL = 0, ecoLpR = 0, xAcc = 0, pLo = 0, pRo = 0, pLn = 0, pRn = 0, cmpGT = 1;
uint32_t pFase = 0; int djModo = 0; SvfS djL, djR; Rampa djRamp;
// HAT / GÜIRO / CLAVE / SILBIDO
float hAmp = 0, hDec = 0.99f, hGL = 1, hGR = 1; SvfS hF; Coef hK;
float gAmp = 0, gDec = 0.99f, gGL = 1, gGR = 1; SvfS gF; Coef gK;
float cvAmp = 0, cvDec = 0.99f; uint32_t cvPh = 0;
uint32_t silPh = 0; const uint32_t silInc = (uint32_t)(3500.0f * (4294967296.0f / 44100.0f));
// ROBOT
uint32_t rPh = 0; uint32_t rInc = 0; float rAmp = 0, rDec = 0.999f, rPit = 0;
SvfS rF; Coef rK;
// RISER (ruido en barrido + tono ascendente) e IMPACTO (caída de sub + ráfaga de ruido)
SvfS risF; Coef risK; uint32_t risPh = 0, risInc = 0; float risAmp = 0, risProg = 0;
uint32_t impPh = 0; float impAmp = 0, impPit = 0, impNz = 0; SvfS impF; Coef impK;
float duckSub = 0;
// ganancias de sección suavizadas
float gSec[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
float gTog[3] = {1, 1, 1};
float corteS = 1500.0f, fxS = 0.3f;
int transSemi = 0; float F0t = F0;

// ==============================================================================================================================================
// RACK — eco (corchea = 6 ticks, ping-pong, oscuro, dos cabezas con crossfade), placa de Dattorro, repetidor de bloque
// ==============================================================================================================================================
#define ECO_N 15500                                                  // 6 ticks al tempo más lento (86 BPM)
int16_t ecoL[ECO_N], ecoR[ECO_N];
int ecoW = 0, ecoDA = 14000, ecoDB = 14000; float ecoX = 0;
const float ECO_XF = 1.0f / (0.040f * SAMPLE_RATE);
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

#define REP_N 7800                                                   // 1/16 de nota al tempo más lento
int16_t repL[REP_N], repR[REP_N];
int repW = 0, repIni = 0, repLen = 1, repPos = 0, repFrozenW = 0; uint32_t repHold = 0; bool repAct = false;

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
bool aGrupo[3] = {true, true, true};
bool aRepeat = false, aThrow = false, aFreeze = false, aRiser = false, aRiserPrev = false;
uint32_t aSecSeen = 0, aMutSeen = 0, aFillSeen = 0, aMascSeen = 0;
int seccion = 0, mascara = 0, tempoIdx = 2;
bool fillActivo = false; uint32_t fillIni = 0, fillFin = 0;
bool swingOn = false; float humano = 0;
float dcXL = 0, dcYL = 0, dcXR = 0, dcYR = 0;
const float DC_R = 0.9986f;
float vVol = 0.6f, vFx = 0.4f, vThrow = 0.0f, vPlacaY = 0.0f, thrG = 0, frzG = 0;
float cmpEnv = 0, cmpGan = 1;
bool benchForce = false;

static i2s_chan_handle_t tx_chan;

void resetAudio() {
  memset(ia1, 0, sizeof(ia1)); memset(ia2, 0, sizeof(ia2)); memset(ia3, 0, sizeof(ia3)); memset(ia4, 0, sizeof(ia4));
  memset(lAp1, 0, sizeof(lAp1)); memset(lD1, 0, sizeof(lD1)); memset(lAp2, 0, sizeof(lAp2)); memset(lD2, 0, sizeof(lD2));
  memset(rAp1, 0, sizeof(rAp1)); memset(rD1, 0, sizeof(rD1)); memset(rAp2, 0, sizeof(rAp2)); memset(rD2, 0, sizeof(rD2));
  memset(ecoL, 0, sizeof(ecoL)); memset(ecoR, 0, sizeof(ecoR)); memset(repL, 0, sizeof(repL)); memset(repR, 0, sizeof(repR));
  lpL = lpR = bwIn = fbL = fbR = 0; eHpL = eHpR = 0;
  orgF = hF = gF = rF = cjF = risF = impF = djL = djR = SvfS{0, 0}; aireA = aireB = ecoLpL = ecoLpR = xAcc = pLo = pRo = pLn = pRn = 0; pFase = 0;
  kAmp = tAmp = hAmp = gAmp = cvAmp = rAmp = cjAmp = bEnv = risAmp = impAmp = 0; bGate = false; bFin = -1;
  dcXL = dcYL = dcXR = dcYR = 0; cmpEnv = 0; cmpGan = 1; duckSub = 0; ecoW = 0; tick = 0; tickAcc = 0; repW = 0; repAct = false;
}

IRAM_ATTR void aplicarTempo(int idx) {                               // cambia el tempo; el eco se reacomoda con dos cabezas y crossfade (sin cambio de tono)
  tempoIdx = idx;
  tickLen = SR * 60.0f / (TEMPOS[idx] * 12.0f);
  int nuevo = (int)(6.0f * tickLen);
  if (nuevo > ECO_N - 4) nuevo = ECO_N - 4;
  if (nuevo != ecoDA) { ecoDB = ecoDA; ecoDA = nuevo; ecoX = 1.0f; }
}

// ─── armonía: acorde en curso → frecuencias del órgano y del bajo ───
IRAM_ATTR float notaHz(int grado, int extraSemi, int octavas) {
  int s = FRIGIA[(grado - 1) % 7] + extraSemi;
  return F0t * powf(2.0f, (float)s / 12.0f) * (float)(1 << octavas);
}
IRAM_ATTR void aplicarAcorde() {
  int d = PROG[(tick / 48 / 4) & 3];                                 // el acorde cambia cada 4 compases
  float f[3] = { notaHz(d, 0, 1), notaHz(d, 7, 1), notaHz(d, 12, 1) };   // fundamental, quinta, octava
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
  mutarCapa(capa[C_CAJA], 0, 2, prof, false);
  mutarCapa(capa[C_GUIRO], 1, 1, prof, false);
  mutarCapa(capa[C_CLAVE], 2, 4, prof, false);
  mutarCapa(capa[C_ROBOT], 0, 3, prof, false);
  int cambios = 2 + (int)(prof * 4.0f);                              // el riff del robot también muta: se re-tiran algunas de sus 8 notas
  for (int i = 0; i < cambios; i++) riff[(int)(rnd01() * 7.99f)] = (uint8_t)(rnd01() * 5.99f);
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
IRAM_ATTR void dispararCaja(float v) {
  cjAmp = v; cjTono = 0.7f * v; cjPh = 0;
  cjDec = expf(-1.0f / (gen[G_CDEC] * SR));
  float p = rnd01() - 0.5f; cjGL = 1.0f - 0.30f * p; cjGR = 1.0f + 0.30f * p;
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
  int ix = riff[riffPos & 7]; riffPos++;                              // el riff avanza una nota por golpe
  float f = F0t * powf(2.0f, (float)(ROBOT_N[ix]) / 12.0f) * 4.0f;   // dos octavas sobre el bajo
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
  int dd = 1; for (; dd <= (int)c.n; dd++) if (golpeEff(C_BAJO, (uint32_t)(paso + dd))) break;
  bFin = (int)((float)(dd * c.div) * tickLen * 0.92f);
}
IRAM_ATTR void dispararImpacto() {                                   // caída de sub + ráfaga de ruido: el "boom" tras un riser o un relleno
  impAmp = 1.0f; impPit = 1.0f; impNz = 1.0f; impPh = 0;
  duckSub = 1.0f;
}

IRAM_ATTR void dispararCapa(int l, uint32_t st) {
  float hv = 1.0f + humano * (rnd01() - 0.5f) * 0.5f;                // humanización de la velocidad
  switch (l) {
    case C_KICK:  dispararKick(); break;
    case C_TOM:   dispararTom((0.6f + 0.4f * rnd01()) * hv); break;
    case C_CAJA:  dispararCaja((0.7f + 0.3f * rnd01()) * hv); break;
    case C_BAJO:  dispararBajo((int)st); break;
    case C_HAT:   dispararHat((0.55f + 0.35f * rnd01()) * hv, (tick % 12) == 6 && rnd01() < 0.3f); break;
    case C_GUIRO: dispararGuiro(((st & 3) == 0 ? 0.9f : 0.5f) * hv, (st & 1) == 0); break;
    case C_CLAVE: dispararClave(0.55f * hv); break;
    case C_ROBOT: dispararRobot(); break;
  }
}

IRAM_ATTR void iniciarRelleno() {                                    // el redoble ocupa lo que queda del compás (al menos un tiempo) y termina en IMPACTO
  fillIni = tick;
  uint32_t finCompas = ((tick / 48) + 1) * 48;
  if (finCompas - tick < 12) finCompas += 48;
  fillFin = finCompas; fillActivo = true;
}

IRAM_ATTR void pasoTick() {
  for (int l = 0; l < NCAPAS; l++) if (pendT[l] == tick) { dispararCapa(l, pendS[l]); pendT[l] = 0xFFFFFFFFu; }   // golpes retrasados por el swing
  if (fillActivo) {
    if (tick >= fillFin) { fillActivo = false; dispararImpacto(); }
    else if ((tick % 3) == 0) {
      float prog = (float)(tick - fillIni) / (float)(fillFin - fillIni);
      float v = 0.35f + 0.65f * prog;
      if (((tick / 3) & 1) == 0) dispararTom(v); else dispararCaja(v);
    }
  }
  for (int l = 0; l < NCAPAS; l++) {
    if (fillActivo && GRUPO[l] != GR_ARM && l != C_CLAVE) continue;   // el relleno reemplaza a la batería (no al bajo ni al robot)
    if (!actC[l]) { if (l == C_BAJO) { bGate = false; bFin = -1; } continue; }
    const Capa &c = capa[l];
    if ((tick % c.div) != 0) continue;
    uint32_t st = tick / c.div;
    if (!golpeEff(l, st)) continue;
    if (swingOn && c.div == 3 && (st & 1)) { pendT[l] = tick + 1; pendS[l] = st; }   // swing: la semicorchea impar se retrasa un tick (1/3 de semicorchea)
    else dispararCapa(l, st);
  }
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
  int capa = capaCtl;
  if (capa == 0) {
    leds[0] = CHSV(28, 255, (uint8_t)(60 + g_mutFlash * 170.0f));                                   // capa A: naranja
    int m = g_mask;
    bool gr = (m != 1 && m != 5 && m != 6), ag = (m != 2 && m != 4 && m != 6), ar = (m != 3 && m != 4 && m != 5);
    leds[1] = CHSV(0,   255, gr ? (uint8_t)(25 + g_kick * 230.0f) : 3);
    leds[2] = CHSV(40,  255, ag ? (uint8_t)(25 + g_hat  * 230.0f) : 3);
    leds[3] = CHSV(150, 255, ar ? (uint8_t)(25 + g_arm  * 230.0f) : 3);
    leds[4] = g_fill ? CRGB(255, 255, 255) : CRGB(CHSV(SEC[g_sec].hue, 255, (uint8_t)(30 + g_pulso * 200.0f)));
  } else {
    leds[0] = CHSV(130, 255, (uint8_t)(60 + g_mutFlash * 170.0f));                                  // capa B: cian
    leds[1] = g_rep ? CRGB(255, 120, 0) : CRGB(2, 1, 0);
    leds[2] = g_thr ? CRGB(255, 255, 0) : CRGB(2, 2, 0);
    leds[3] = g_frz ? CRGB(190, 215, 255) : CRGB(1, 1, 3);
    leds[4] = CHSV(215, 255, (uint8_t)(4 + g_ris * 250.0f));
  }
  float dj = potDj;
  if (g_sinRecoger) leds[5] = CRGB(255, 120, 0);
  else if (dj < 0.45f)      leds[5] = CHSV(160, 255, 40 + (uint8_t)((0.45f - dj) / 0.45f * 200.0f));
  else if (dj > 0.55f) leds[5] = CHSV(215, 255, 40 + (uint8_t)((dj - 0.55f) / 0.45f * 200.0f));
  else                 leds[5] = CRGB(30, 30, 30);
  if (xrunAt != 0 && millis() - xrunAt < 1500) leds[5] = CRGB(255, 0, 0);
  onboard[0] = CHSV(hue, 255, 20 + (uint8_t)(g_kick * 120.0f + g_arm * 100.0f));
  onboard[0].nscale8(ONBOARD_BRIGHT);
  if (flashLevel > 0.02f) { uint8_t w = (uint8_t)(flashLevel * 130.0f); for (int i = 0; i < NUM_LEDS; i++) leds[i] += CRGB(w, w, w); flashLevel *= 0.62f; }
  FastLED.show();
}

// ─── Pots de capa con RECOGIDA: valor guardado por capa y pot; no actúan hasta que la perilla lo alcanza ───
float guardado[2][3] = { { 2.5f / 7.0f, 0.5f, 0.2f }, { 0.5f, 0.35f, 0.0f } };
bool  recogido[2][3] = { { true, true, true }, { false, false, false } };
bool  primeraLectura[3] = { true, true, true };
void escribirParam(int capa, int p, float v) {
  if (capa == 0) { if (p == 0) potTempo = v; else if (p == 1) potDens = v; else potGroove = v; }
  else           { if (p == 0) potDj = v;    else if (p == 1) potFx = v;   else potMut = v; }
}
void leerPotCapa(int p, float v) {                 // p = 0..2 ↔ POT2..POT4
  int capa = capaCtl;
  if (primeraLectura[p]) { primeraLectura[p] = false; guardado[0][p] = v; recogido[0][p] = true; escribirParam(0, p, v); }   // al encender, la capa A toma la perilla
  if (!recogido[capa][p] && fabsf(v - guardado[capa][p]) < 0.05f) recogido[capa][p] = true;
  if (recogido[capa][p]) { guardado[capa][p] = v; escribirParam(capa, p, v); }
  bool alguno = false;
  for (int q = 0; q < 3; q++) if (!recogido[capa][q]) alguno = true;
  g_sinRecoger = alguno;
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

  if (b1 == LOW && b1L == HIGH && t - b1T > DEBOUNCE_MS) {          // BTN1 → capa siguiente; los pots de la nueva capa quedan "sin recoger"
    b1T = t;
    int n = capaCtl ^ 1;
    for (int q = 0; q < 3; q++) recogido[n][q] = false;
    capaCtl = n; flashLevel = 0.9f;
  }
  int capa = capaCtl;
  bool f2 = (b2 == LOW && b2L == HIGH && t - b2T > DEBOUNCE_MS); if (f2) b2T = t;
  bool f3 = (b3 == LOW && b3L == HIGH && t - b3T > DEBOUNCE_MS); if (f3) b3T = t;
  bool f4 = (b4 == LOW && b4L == HIGH && t - b4T > DEBOUNCE_MS); if (f4) b4T = t;
  bool f5 = (b5 == LOW && b5L == HIGH && t - b5T > DEBOUNCE_MS); if (f5) b5T = t;
  if (capa == 0) {
    if (f2) { reqSeccion = reqSeccion + 1; flashLevel = 1.0f; }
    if (f3) { reqMutar   = reqMutar   + 1; flashLevel = 0.8f; }
    if (f4) { reqFill    = reqFill    + 1; flashLevel = 0.8f; }
    if (f5) { reqMasc    = reqMasc    + 1; flashLevel = 0.5f; }
    reqRepeat = reqThrow = reqFreeze = reqRiser = false;
  } else {
    reqRepeat = (b2 == LOW); reqThrow = (b3 == LOW); reqFreeze = (b4 == LOW); reqRiser = (b5 == LOW);   // gestos: mientras se mantienen
  }
  b1L = b1; b2L = b2; b3L = b3; b4L = b4; b5L = b5;
  readIMU();
  static const uint8_t POT_PIN[4] = { POT1, POT2, POT3, POT4 };
  static uint8_t scan = 0;
  int pi = scan; scan = (scan + 1) & 3;
  float v = readPot(POT_PIN[pi]);
  if (potUlt[pi] < 0.0f || fabsf(v - potUlt[pi]) > 0.004f) potUlt[pi] = v;     // banda muerta: el ruido del ADC no mueve nada
  float p = potUlt[pi];
  if (pi == 0) potVol = p; else leerPotCapa(pi - 1, p);
  renderLEDs();
#if DIAG
  static unsigned long lastSecA = 0; if (t - lastSecA > 9000) { lastSecA = t; reqSeccion = reqSeccion + 1; }   // para medir todas las secciones sin pulsar nada
  static unsigned long lastP = 0;
  if (t - lastP > 2000) {
    lastP = t;
    Serial0.printf("DIAG sec=%s  render promedio %u us (%.0f %% del buffer)  maximo %u us (%.0f %%)  retrasos %u\n",
                   SEC[g_sec].nombre, (unsigned)g_usAvg, 100.0f * g_usAvg / BUF_US, (unsigned)g_usMax, 100.0f * g_usMax / BUF_US, (unsigned)g_xruns);
    g_usMax = 0;
    if (ccN) { Serial0.printf("DIAG ciclos/muestra: reloj+suav %u | graves %u | bajo+organo %u | agudos %u | robot+caja %u | seca+eco %u | placa %u | riser+impacto %u | dj+comp+salida %u\n",
      (unsigned)(cc[0]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[1]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[2]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[3]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[4]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[5]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[6]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[7]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[8]/(ccN*BUFFER_SAMPLES)));
      for (int q = 0; q < 10; q++) cc[q] = 0; ccN = 0; }
  }
#endif
}

// ==============================================================================================================================================
// AUDIO
// ==============================================================================================================================================
IRAM_ATTR void renderCore(int16_t *buffer) {
  // ── pedidos de la tarea de control ──
  uint32_t ss = reqSeccion;
  if (ss != aSecSeen) { aSecSeen = ss; seccion = (int)(ss % NSEC); transSemi = SEC[seccion].trans; F0t = F0 * powf(2.0f, (float)transSemi / 12.0f); aplicarAcorde(); }
  uint32_t mc = reqMutar;
  if (mc != aMutSeen) { aMutSeen = mc; mutPendiente = true; }
  uint32_t fl = reqFill;
  if (fl != aFillSeen) { aFillSeen = fl; iniciarRelleno(); }
  uint32_t mk = reqMasc;
  if (mk != aMascSeen) { aMascSeen = mk; mascara = (int)(mk % NMASK); }
  aRepeat = reqRepeat; aThrow = reqThrow; aFreeze = reqFreeze; aRiser = reqRiser;
  if (benchForce || DIAG) { aRepeat = aThrow = aFreeze = aRiser = true; mascara = 0; }            // en diagnóstico todo activo: se mide el peor caso
  if (aRiserPrev && !aRiser) dispararImpacto();                                                  // soltar el riser = IMPACTO
  aRiserPrev = aRiser;

  // tempo: 7 zonas con histéresis
  { float z = potTempo * (float)NTEMPO; int cand = (int)z; if (cand > NTEMPO - 1) cand = NTEMPO - 1;
    if (cand != tempoIdx && fabsf(z - ((float)tempoIdx + 0.5f)) < 0.62f) cand = tempoIdx;
    if (cand != tempoIdx) aplicarTempo(cand); }

  for (int g = 0; g < NG_; g++) gen[g] += (genT[g] - gen[g]) * 0.0020f;
  mutFlash *= 0.93f;

  // máscara de capas y grupos
  aGrupo[GR_GRAVE] = (mascara != 1 && mascara != 5 && mascara != 6);
  aGrupo[GR_AGUDA] = (mascara != 2 && mascara != 4 && mascara != 6);
  aGrupo[GR_ARM]   = (mascara != 3 && mascara != 4 && mascara != 5);

  // ── objetivos por buffer (aquí sí valen tanf/powf: se pagan 1 vez cada 128 muestras) ──
  const Seccion &S = SEC[seccion];
  float secT[10] = { S.kick, S.tom, S.caja, S.hat, S.guiro, S.bajo, S.organo, S.robot, S.silbido, S.aire };
  for (int i = 0; i < 10; i++) gSec[i] += (secT[i] - gSec[i]) * 0.0030f;        // ~1.5 s por transición de sección
  for (int i = 0; i < 3; i++) gTog[i] += ((aGrupo[i] ? 1.0f : 0.0f) - gTog[i]) * 0.020f;
  corteS += (S.corte - corteS) * 0.0040f; fxS += (S.fx - fxS) * 0.0040f;
  const float gOrg = gSec[6] * gTog[GR_ARM], gSil = gSec[8] * gTog[GR_AGUDA], gAire = gSec[9] * gTog[GR_AGUDA];
  float gVoz[NCAPAS];
  for (int l = 0; l < NCAPAS; l++) { gVoz[l] = gSec[GSEC_IX[l]] * gTog[GRUPO[l]]; actC[l] = (gVoz[l] > 0.01f) && aGrupo[GRUPO[l]]; }
  // densidad y groove
  float dens = 0.45f + 1.10f * potDens;
  for (int l = 0; l < NCAPAS; l++) {
    int k = capa[l].k;
    if (USA_DENS[l]) { k = (int)((float)k * dens + 0.5f); if (k < 1) k = 1; if (k > capa[l].n) k = capa[l].n; }
    kEff[l] = (uint8_t)k;
  }
  swingOn = potGroove > 0.35f; humano = potGroove;

  float dj = potDj;                                                    // filtro DJ: un solo SVF por canal, en modo LP (izquierda) o HP (derecha)
  Coef djT;
  if (dj < 0.45f)      { float x = (0.45f - dj) / 0.45f; djModo = 0; djT = coefDe(gDe(20000.0f * powf(0.0040f, x)), 1.0f / (0.707f + 0.45f * x)); }
  else if (dj > 0.55f) { float x = (dj - 0.55f) / 0.45f; djModo = 1; djT = coefDe(gDe(20.0f * powf(180.0f, x)),    1.0f / (0.707f + 0.35f * x)); }
  else                 { djModo = 0; djT = coefDe(gDe(20000.0f), 1.4142f); }
  rampaHacia(djRamp, djT);
  rampaHacia(orgRamp, coefDe(gDe(corteS), 1.0f / 0.9f));

  // riser: progreso 0..1 (al mantener tarda ~8 s; en un relleno llega a 1 justo al final del compás)
  bool risActivo = aRiser || fillActivo;
  if (fillActivo) risProg = (float)(tick - fillIni) / (float)(fillFin - fillIni);
  else if (aRiser) { risProg += (float)BUFFER_SAMPLES / (8.0f * SR); if (risProg > 1.0f) risProg = 1.0f; }
  else risProg = 0.0f;
  if (risActivo) {
    risK = coefDe(gDe(350.0f * powf(26.0f, risProg)), 1.0f / 3.0f);   // ruido en barrido de 350 Hz a 9 kHz con Q alta
    risInc = incF(160.0f * powf(9.0f, risProg));                      // tono que sube de 160 Hz a 1.4 kHz
  }
  impK = coefDe(gDe(1700.0f), 1.4142f);

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
  cjK = coefDe(gDe(2200.0f), 1.0f / 0.9f);
  const float aireNivel = gen[G_AIRE] * 0.060f * gAire;
  const float autoProf = 0.15f + 0.85f * potMut;
  const float decayBase = 0.52f + 0.12f * vFx + 0.06f * vPlacaY;     // la placa corre a 22 kHz: cada vuelta dura el doble, por eso el coeficiente es menor
  float acumK = 0, acumH = 0, acumA = 0;
  for (int k = 0; k < 3; k++) { oInc[k] += (int32_t)(oIncT[k] - oInc[k]) / 32; }

  // repetidor: arranque y fin
  if (aRepeat && !repAct) {
    repAct = true; repFrozenW = repW; repHold = 0;
    repLen = (int)(3.0f * tickLen); if (repLen > REP_N) repLen = REP_N;
    repIni = (repFrozenW - repLen + REP_N) % REP_N; repPos = 0;
  } else if (!aRepeat && repAct) repAct = false;

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
    thrG += ((aThrow ? 1.0f : 0.0f) - thrG) * 0.0030f;
    frzG += ((aFreeze ? 1.0f : 0.0f) - frzG) * 0.0030f;
    const float nz = ruido();                                           // un solo ruido por muestra para todas las voces de ruido
    MARCA(0)

    // ── GRAVES: KICK + TOM (sólo si suenan) ──
    float kick = 0, tom = 0;
    if (kAmp > 0.0004f) {
      kPh += incF(58.0f + 82.0f * kPit);
      float hp = nz - kPrev; kPrev = nz;
      kick = (sinPh(kPh) * 0.78f * kAmp + (kClk > 0.01f ? hp * kClk * 0.18f * kAmp : 0.0f)) * gVoz[C_KICK];
      kPit *= 0.9990f; kAmp *= kDec; kClk *= 0.9946f;
    } else kAmp = 0;
    duckSub += ((kAmp > 0.2f ? 1.0f : 0.0f) - duckSub) * 0.0020f;
    if (tAmp > 0.0004f) {
      tPh += incF(115.0f + 75.0f * tPit);
      float hp = nz - tPrev; tPrev = nz;
      tom = (sinPh(tPh) * tAmp * 0.55f + hp * tSlap * 0.12f) * (fillActivo ? 1.0f : gVoz[C_TOM]);
      tPit *= 0.9988f; tAmp *= tDec; tSlap *= 0.992f;
    } else tAmp = 0;
    MARCA(1)

    // ── BAJO (una tabla horneada) y ÓRGANO (3 notas de sierra limitada, un solo filtro) ──
    float bajo = 0;
    if (bGate) bEnv += (1.0f - bEnv) * 0.0042f; else { bEnv *= 0.9990f; if (bEnv < 0.0005f) bEnv = 0; }
    if (bEnv > 0) {
      bInc += (int32_t)(bIncT - bInc) >> 6;
      bPh += bInc;
      bajo = tabPh(BASSW, bPh) * 0.60f * bEnv * (1.0f - 0.55f * duckSub) * gVoz[C_BAJO];
    }
    float org = 0;
    if (gOrg > 0.002f) {
      float sum = 0;
      for (int k = 0; k < 3; k++) { oPh[k] += oInc[k]; sum += leeTabla(tabSaw[oMip[k]], oPh[k]); }
      org = svfLP(orgF, orgRamp.cur, sum * 0.16f) * gOrg * (1.0f - 0.35f * duckSub);
    }
    MARCA(2)

    // ── AGUDOS: HAT / GÜIRO / CLAVE / AIRE / SILBIDO ──
    float hat = 0, gui = 0, cvv = 0, aire = 0, sil = 0;
    if (hAmp > 0.0004f) { hat = svfBP(hF, hK, nz) * hAmp * 0.24f * gVoz[C_HAT]; hAmp *= hDec; } else hAmp = 0;
    if (gAmp > 0.0004f) { gui = svfBP(gF, gK, nz) * gAmp * 0.22f * gVoz[C_GUIRO]; gAmp *= gDec; } else gAmp = 0;
    if (cvAmp > 0.0004f) { cvPh += incF(2500.0f); cvv = sinPh(cvPh) * cvAmp * 0.16f * gVoz[C_CLAVE]; cvAmp *= cvDec; } else cvAmp = 0;
    if (aireNivel > 0.0004f) { aireA += 0.35f * (nz - aireA); aireB += 0.06f * (aireA - aireB); aire = (aireA - aireB) * aireNivel; }
    if (gSil > 0.002f) { silPh += silInc; sil = sinPh(silPh) * 0.045f * gSil; }
    MARCA(3)

    // ── ROBOT (riff) y CAJA ──
    float rob = 0, caja = 0;
    if (rAmp > 0.0004f) {
      rPh += rInc + (uint32_t)((float)rInc * 0.06f * rPit);
      rob = svfLP(rF, rK, tabPh(ROBW, rPh)) * rAmp * 0.20f * gVoz[C_ROBOT];
      rPit *= 0.9990f; rAmp *= rDec;
    } else rAmp = 0;
    if (cjAmp > 0.0004f) {
      cjPh += incF(185.0f);
      caja = (svfBP(cjF, cjK, nz) * cjAmp * 0.30f + sinPh(cjPh) * cjTono * 0.28f) * (fillActivo ? 1.0f : gVoz[C_CAJA]);
      cjAmp *= cjDec; cjTono *= 0.9960f;
    } else cjAmp = 0;
    MARCA(4)

    // ── BUS SECO y ECO ping-pong (corchea; dos cabezas con crossfade; filtros de un polo) ──
    float centro = kick * 0.95f + bajo + tom * 0.7f + rob * 0.6f + org;
    float dryL = centro + hat * hGL + gui * gGL + cvv * 0.8f + aire + tom * 0.3f * tGL + caja * cjGL + sil;
    float dryR = centro + hat * hGR + gui * gGR + cvv * 1.2f + aire * 0.9f + tom * 0.3f * tGR + caja * cjGR + sil;
    int rA = ecoW - ecoDA; if (rA < 0) rA += ECO_N;
    float oL = ecoL[rA] * (1.0f / 32768.0f), oR = ecoR[rA] * (1.0f / 32768.0f);
    if (ecoX > 0.0f) {
      int rB = ecoW - ecoDB; if (rB < 0) rB += ECO_N;
      oL += (ecoL[rB] * (1.0f / 32768.0f) - oL) * ecoX; oR += (ecoR[rB] * (1.0f / 32768.0f) - oR) * ecoX;
      ecoX -= ECO_XF; if (ecoX < 0.0f) ecoX = 0.0f;
    }
    ecoLpL += 0.26f * (oL - ecoLpL); ecoLpR += 0.26f * (oR - ecoLpR);
    eHpL += ECO_HP * (ecoLpL - eHpL); float lzL = ecoLpL - eHpL;
    eHpR += ECO_HP * (ecoLpR - eHpR); float lzR = ecoLpR - eHpR;
    float fbT = 0.42f + 0.22f * vFx + 0.22f * vThrow + 0.30f * thrG; if (fbT > 0.88f) fbT = 0.88f;
    ecoFb += (fbT - ecoFb) * 0.0006f;
    const float ecoSend = 0.10f + 0.55f * vFx + 0.35f * vThrow + 0.60f * thrG;
    float sL = hat * hGL * 0.9f + gui * gGL * 0.6f + cvv + rob + tom * tGL * 0.7f + caja * cjGL * 0.8f + kick * 0.15f + sil * 0.5f + dryL * 0.40f * thrG;
    float sR = hat * hGR * 0.9f + gui * gGR * 0.6f + cvv + rob + tom * tGR * 0.7f + caja * cjGR * 0.8f + kick * 0.15f + sil * 0.5f + dryR * 0.40f * thrG;
    float wL = sL * ecoSend + lzR * ecoFb, wR = sR * ecoSend + lzL * ecoFb;
    if (wL > 0.8f || wL < -0.8f) wL = limitador(wL);
    if (wR > 0.8f || wR < -0.8f) wR = limitador(wR);
    ecoL[ecoW] = (int16_t)(wL * 30000.0f); ecoR[ecoW] = (int16_t)(wR * 30000.0f);
    if (++ecoW >= ECO_N) ecoW = 0;
    float retEL = lzL * (0.45f + 0.55f * vFx), retER = lzR * (0.45f + 0.55f * vFx);
    MARCA(5)

    // ── RISER e IMPACTO (activos sólo mientras suenan) ──
    float ris = 0, imp = 0;
    if (risActivo || risAmp > 0.001f) {
      float objetivo = risActivo ? risProg * risProg : 0.0f;
      risAmp += (objetivo - risAmp) * (risActivo ? 0.0020f : 0.0030f);
      if (risAmp > 0.001f) { risPh += risInc; ris = (svfBP(risF, risK, nz) * 0.9f + sinPh(risPh) * 0.35f) * risAmp * 0.30f; }
    }
    if (impAmp > 0.0004f) {
      impPh += incF(30.0f + 60.0f * impPit);
      imp = sinPh(impPh) * impAmp * 0.85f + svfLP(impF, impK, nz) * impNz * 0.55f;
      impPit *= 0.99994f; impAmp *= 0.99996f; impNz *= 0.99985f;
    } else impAmp = 0;
    MARCA(7)

    // ── PLACA (Dattorro) a 22.05 kHz: se calcula una muestra de cada dos y se interpola ──
    xAcc += ((sL + sR) * 0.5f + (retEL + retER) * 0.35f + org * 0.10f + ris * 0.5f + imp * 0.6f);
    if (pFase == 0) { pFase = 1; }
    else {
      pFase = 0;
      float envio = (0.015f + 0.18f * vFx + 0.30f * vPlacaY) * (1.0f - frzG);       // INFINITO: deja de entrar señal
      float x = xAcc * 0.5f * envio * 3.0f; xAcc = 0;
      bwIn += 0.80f * (x - bwIn);
      float decay = decayBase + (0.97f - decayBase) * frzG;                          // INFINITO: la cola casi no pierde
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
    const float colaG = 0.22f + 0.55f * vFx + 0.50f * vPlacaY + 0.30f * frzG;
    MARCA(6)

    // ── MEZCLA → FILTRO DJ (un SVF por canal) → DC → compresor → volumen → limitador ──
    float mL = dryL + ris + imp + retEL * (0.5f + vThrow + thrG) + pL * colaG;
    float mR = dryR + ris + imp + retER * (0.5f + vThrow + thrG) + pR * colaG;
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

    // ── REPETIR: graba la salida y, mientras se mantiene, repite una rebanada cada vez más corta ──
    if (!repAct) {
      repL[repW] = (int16_t)(oL2 * 30000.0f); repR[repW] = (int16_t)(oR2 * 30000.0f);
      if (++repW >= REP_N) repW = 0;
    } else {
      int idx = repIni + repPos; if (idx >= REP_N) idx -= REP_N;
      float w = 1.0f;
      if (repPos < 48) w = (float)repPos * (1.0f / 48.0f);
      else if (repPos > repLen - 48) w = (float)(repLen - repPos) * (1.0f / 48.0f);
      if (w < 0.0f) w = 0.0f;
      oL2 = repL[idx] * (1.0f / 30000.0f) * w; oR2 = repR[idx] * (1.0f / 30000.0f) * w;
      if (++repPos >= repLen) {
        repPos = 0;
        repHold += (uint32_t)repLen;
        int quiere = (int)(3.0f * tickLen);
        if (repHold > (uint32_t)(3.0f * SR)) quiere = (int)(0.75f * tickLen); else if (repHold > (uint32_t)(1.5f * SR)) quiere = (int)(1.5f * tickLen);
        if (quiere > REP_N) quiere = REP_N;
        if (quiere < 64) quiere = 64;
        repLen = quiere; repIni = (repFrozenW - repLen + REP_N) % REP_N;
      }
    }

    uint32_t r1 = xr(), r2 = xr();                                      // dither triangular ±1 LSB: dos uniformes de 16 bits por canal con un solo número aleatorio
    float dL = (float)((int)(r1 & 0xFFFF) - (int)(r1 >> 16)) * (1.0f / 65536.0f);
    float dR = (float)((int)(r2 & 0xFFFF) - (int)(r2 >> 16)) * (1.0f / 65536.0f);
    buffer[i * 2]     = (int16_t)((int)(oL2 * 32000.0f + dL + 32768.5f) - 32768);
    buffer[i * 2 + 1] = (int16_t)((int)(oR2 * 32000.0f + dR + 32768.5f) - 32768);
    MARCA(8)

    acumK += kAmp; acumH += hAmp + gAmp; acumA += bEnv + gOrg * 0.3f;
  }
#if DIAG
  ccN++;
#endif
  float kk = acumK * (1.0f / BUFFER_SAMPLES); if (kk > 1) kk = 1;
  float hh = acumH * (1.0f / BUFFER_SAMPLES) * 1.4f; if (hh > 1) hh = 1;
  float aa = acumA * (1.0f / BUFFER_SAMPLES); if (aa > 1) aa = 1;
  g_kick = kk; g_hat = hh; g_arm = aa; g_mutFlash = mutFlash; g_sec = seccion; g_mask = mascara;
  g_ris = risAmp > 1.0f ? 1.0f : risAmp; g_rep = repAct; g_thr = aThrow; g_frz = aFreeze; g_fill = fillActivo;
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
  cargarPatrones();
  resetAudio();
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812, ONBOARD_PIN, GRB>(onboard, 1);
  FastLED.setBrightness(LED_BRIGHT); FastLED.clear(); FastLED.show();
  imuOk = initIMU(false); delay(30); readIMU();
  aplicarTempo(2); ecoDB = ecoDA; ecoX = 0;
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
