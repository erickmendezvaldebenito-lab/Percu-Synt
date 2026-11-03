// ==============================================================================================================================================
// PERCU-SYNTH — Techno en Capas (techno ácido oscuro: BAJO · RITMO · SONIDOS, cada capa independiente y todas coordinadas) — GC Lab Chile
// ==============================================================================================================================================
// HARDWARE
// ==============================================================================================================================================
// - ESP32-S3 + DAC PCM5102 (I2S: LCK 39, DIN 40, BCK 41) + 5 botones + 4 pots + 6 LEDs SMD WS2812
// ==============================================================================================================================================
// ARDUINO IDE
// ==============================================================================================================================================
// - Board: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · Monitor Serie a 115200 baudios
// - Librerías: FastLED (gestor de librerías de Arduino)
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// "Nocturno": una canción de techno ácido, oscuro e hipnótico en Fa menor (en la línea de Daniel Avery: bajo ácido que respira, bombo seco
// afinado a la armonía, hats a contratiempo, pad con cola larga y arpegio). Sólo la ESTÉTICA: todo se sintetiza aquí, sin samples ni melodías
// de nadie. Seis acordes (Fm7 · Bbm7 · Cm7 · Dbmaj7 · Abmaj7 · Ebmaj7) en cuatro progresiones: FADE (un acorde, 16 compases) · ALBA (i-VI) ·
// NOCHE (i-iv-i-v) · VIAJE (i-VI-III-VII).
//
// TRES CAPAS, CADA UNA CON SU PROPIO RELOJ. Reglas que valen IGUAL en las tres capas:
//   BTN1 = pasa a la capa siguiente (BAJO → RITMO → SONIDOS)       POT1 = VOLUMEN de la capa
//   BTN2 = STOP / PLAY de ESA capa (las otras siguen)             POT2 = TEMPO de ESA capa (110–150 BPM de 1 en 1)
//
//   BAJO      POT3 corte · POT4 ácido                  | BTN3 patrón (8) · BTN4 ácido/sub · BTN5 octava
//   RITMO     POT3 punch del bombo · POT4 hats         | BTN3 groove (8) · BTN4 relleno · BTN5 percusión on/off
//   SONIDOS   POT3 brillo · POT4 espacio (eco+reverb)  | BTN3 progresión (4) · BTN4 textura (mantener; al soltar, IMPACTO) · BTN5 arpegio (sube/baja/sube-baja/azar/apagado)
//
// Las tres capas arrancan con el mismo tempo y en fase. Si cambias el tempo de una, esa capa se va por su cuenta (polirritmia). Para
// volver a juntarlas: iguala los BPM y haz STOP y PLAY en la capa que se desfasó: al volver a entrar espera el inicio de compás de una capa
// que sí suene y arranca justo ahí. COORDINACIÓN: la armonía la lleva SONIDOS (su reloj cuenta los compases de la progresión) y manda al
// bajo, al pad, al arpegio y al bombo (que cae en la fundamental del acorde); el bombo hace pumping sobre bajo, pad, arpegio y la cola de la
// reverb; el eco sigue el tempo de SONIDOS. Patrones en el siguiente tiempo de su capa, progresión en el siguiente compás.
//
// PANTALLA OLED (SSD1306, I2C 21/38; opcional): al cambiar de capa anuncia "CAPA n DE 3" y después muestra el mapa: nombre y valor de cada
// perilla, función de cada botón, estado de la capa y el último gesto. LED 5 ámbar = alguna perilla en escalado de valor.
// Perillas: al volver a una capa NO saltan (escalado de valor); una raya en la pantalla marca dónde está la perilla hasta que coincide.
//
// MOTOR LIVIANO (medido en la placa): fase entera, tablas, filtros SVF con coeficientes por bloque interpolados, cero floorf/expf/
// powf/tanf/divisiones por muestra, código caliente en IRAM, reverb de placa a media frecuencia, compresor que no actúa y limitador
// que casi nunca. Con -DDIAG=1 imprime por Serial0 la carga de CPU y el nivel (el USB de esta placa es un puente UART).
// ==============================================================================================================================================
// FUNCIONAMIENTO: ver el mapa de controles arriba.
// ==============================================================================================================================================

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <FastLED.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>

#ifndef DIAG
#define DIAG 0
#endif

// ─── Tipos (arriba del todo para que el IDE de Arduino genere bien los prototipos) ───
struct Coef { float a1, a2, a3, k; };                // SVF TPT: coeficientes
struct SvfS { float ic1, ic2; };                      // SVF TPT: estado
struct Rampa { Coef cur, paso; };                     // coeficientes que se interpolan por muestra

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
// MÚSICA — canción en Fa menor; cada capa con su reloj de 24 ticks por tiempo (la semicorchea son 6 ticks)
// ==============================================================================================================================================
const int NTEMPO = 41;                                              // 110, 111, … 150 BPM de 1 en 1: CADA CAPA tiene su propio tempo
const int ZONA_INI = 14;                                            // 124 BPM al encender (las tres capas arrancan iguales y en fase)
inline float bpmDe(int z) { return 110.0f + (float)z; }
#define TPB 24                                                      // ticks por tiempo
#define TPS 6                                                       // ticks por semicorchea
#define TPC 96                                                      // ticks por compás

// Bajo: 16 pasos. r raíz · w segunda menor · t tercera del acorde · f quinta · s séptima · o octava · mayúscula = acento · '-' silencio.
// Slides: '/' = la nota entra deslizada desde la anterior
#define NBAJOPAT 8
const char *BAJO_N[NBAJOPAT] = { "--R---r---R---r-", "-rRr-rRr-rRr-rwr", "r--r--R--r-t--f-", "R-------r-----t-", "r--r-r--r--r-r-f", "-r-o-r-o-r-o-r-t", "rr-rr-rr-rr-rr-r", "r-r-r-r-r-r-r-r-" };
const char *BAJO_S[NBAJOPAT] = { "................", "................", ".........../../.", "................", "................", "................", "................", "................" };
const char *BAJO_NOM[NBAJOPAT] = { "OFFBEAT", "RODANDO", "ACIDO", "PEDAL", "SINCOPA", "OCTAVAS", "GALOPE", "NEGRAS" };

// Ritmo: kick · clap · hat (x cerrado, X acento, o abierto) · percusión (a conga aguda, b grave) · shaker (s suave, S fuerte)
#define NRITMO 8
const char *RIT_K[NRITMO] = { "x---x---x---x---", "x---x---x---x---", "x--x--x---x--x--", "x---x---x---x---", "x---x---x---x-x-", "x---x---x---x---", "x---x---x---x---", "x---x---x---x---" };
const char *RIT_C[NRITMO] = { "----x-------x---", "----x-------x---", "----x-------x--x", "----------------", "----x-------x---", "----x-------x---", "----x-------x---", "----------------" };
const char *RIT_H[NRITMO] = { "x-o-x-o-x-o-x-o-", "xxxxXxxxxxxxXxxx", "x-x-xxx-x-x-xx-x", "--x---x---x---x-", "x-xxx-xxx-xxx-xx", "x--x--x--x--x--x", "--o---o---o---o-", "--o---o---o---o-" };
const char *RIT_P[NRITMO] = { "--a---b---a---b-", "--a--b---a--b---", "-a--b--a--b--a--", "-------b-------b", "a---b---a---b-a-", "--a-----b---a---", "a-b-a--ba-b-a-ba", "----------------" };
const char *RIT_S[NRITMO] = { "-s-s-s-s-s-s-s-s", "s-s-s-s-s-s-s-s-", "-s-ss-s--s-ss-s-", "----------------", "-s-s-s-s-s-s-s-s", "s-s-s-s-s-s-s-s-", "----------------", "----------------" };
const char *RIT_NOM[NRITMO] = { "CLASICO", "RODANDO", "ROTO", "MINIMAL", "DOBLE", "SHUFFLE", "TOMS", "DESNUDO" };

// Armonía de la canción (Fa menor, oscura y abierta): raíz en semitonos sobre La; tercera y séptima del acorde; compases por acorde
struct Acorde { int8_t raiz, ter, sep; };
const Acorde FM7 = { -4, 3, 10 }, BBM7 = { 1, 3, 10 }, CM7 = { 3, 3, 10 }, DBM7 = { 4, 4, 11 }, ABM7 = { -1, 4, 11 }, EBM7 = { 6, 4, 11 };
const Acorde *PROG_A[4][4] = { { &FM7, &FM7, &FM7, &FM7 }, { &FM7, &DBM7, &FM7, &DBM7 }, { &FM7, &BBM7, &FM7, &CM7 }, { &FM7, &DBM7, &ABM7, &EBM7 } };
const int PROG_N[4]    = { 1, 2, 4, 4 };
const int PROG_BARS[4] = { 16, 8, 4, 4 };
const char *PROG_NOM[4] = { "FADE", "ALBA", "NOCHE", "VIAJE" };

// ==============================================================================================================================================
// PARÁMETROS Y PEDIDOS (la tarea de control escribe, la de audio aplica)
// REGLAS FIJAS EN LAS TRES CAPAS: BTN1 = cambiar de capa · POT1 = volumen de la capa · BTN2 = STOP/PLAY de la capa · POT2 = tempo de la capa
// ==============================================================================================================================================
#define NCAPAS 3
volatile int      capaCtl = 0;                                      // 0 BAJO · 1 RITMO · 2 SONIDOS
const float P_TEMPO_INI = ((float)ZONA_INI + 0.5f) / (float)NTEMPO;
volatile float pTempoB = P_TEMPO_INI, pTempoR = P_TEMPO_INI, pTempoS = P_TEMPO_INI;
volatile uint32_t reqTogB = 0, reqTogR = 0, reqTogS = 0;            // BTN2: contadores de stop/play de cada capa (la paridad decide; sin escrituras compartidas)
// capa 1 · BAJO: volumen · tempo · corte · ácido
volatile float pVolB = 0.75f, pCorte = 0.45f, pAcid = 0.55f;
volatile uint32_t reqBPat = 0, reqBTipo = 0, reqBOct = 0;
// capa 2 · RITMO: volumen · tempo · punch · hats
volatile float pVolR = 0.75f, pPunch = 0.5f, pHats = 0.5f;
volatile uint32_t reqRPat = 0, reqRFill = 0; volatile bool reqRPerc = true;
// capa 3 · SONIDOS: volumen · tempo · brillo · espacio
volatile float pVolS = 0.75f, pBrillo = 0.5f, pEspacio = 0.35f;
volatile uint32_t reqADir = 0, reqProg = 0; volatile bool reqTex = false;
// constantes de la canción (antes eran perillas)
const float pEvo = 0.6f, pLargo = 0.55f, pPercV = 0.5f, pArpV = 0.5f, pArpDiv = 0.0f;
volatile bool reqRKick = true;
volatile uint8_t g_estado[3] = { 1, 1, 1 };                         // 0 stop · 1 suena · 2 esperando el compás de otra capa
volatile int g_zona[3] = { ZONA_INI, ZONA_INI, ZONA_INI };
volatile float g_pul[3] = { 0, 0, 0 };
volatile bool g_arpOn = true;

volatile bool g_sinRecoger = false;
volatile float g_bajo = 0, g_kick = 0, g_hat = 0, g_arp = 0, g_pad = 0, g_tex = 0;
volatile int g_bPat = 0, g_rPat = 0, g_prog = 0, g_tipo = 0, g_oct = 0, g_dir = 0; volatile bool g_slide = true, g_perc = true, g_kickOn = true, g_padOn = true, g_fill = false;
volatile unsigned long xrunAt = 0;
volatile uint32_t g_usAvg = 0, g_usMax = 0, g_xruns = 0;
#if DIAG
#include <xtensa/hal.h>
unsigned long dgOledMax = 0;
float dgPk = 0; uint32_t dgN = 0, dgSobre = 0, dgComp = 0; double dgGmin = 1;
uint32_t cc[10] = {0,0,0,0,0,0,0,0,0,0}; uint32_t ccN = 0; uint32_t ccT;
#define MARCA(n) { uint32_t _t = xthal_get_ccount(); cc[n] += _t - ccT; ccT = _t; }
#else
#define MARCA(n)
#endif

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
// RELOJ Y ARMONÍA
// ==============================================================================================================================================
float tlB = 900.0f, tlR = 900.0f, tlS = 900.0f, accB = 0, accR = 0, accS = 0;   // reloj PROPIO de cada capa (muestras por tick)
uint32_t tickB = 0, tickR = 0, tickS = 0, barB = 0, barR = 0, barS = 0;
int zB = ZONA_INI, zR = ZONA_INI, zS = ZONA_INI;
bool runB = true, runR = true, runS = true, pendB = false, pendR = false, pendS = false;   // pend = esperando el inicio de compás de otra capa para entrar en fase
uint16_t mBSkip = 0, mBOct = 0, mHSkip = 0, mHAdd = 0, mPAdd = 0;     // variaciones del compás en curso (una máscara de 16 pasos por cada una)
bool bVarOct = false; float percK = 1.0f, arpK = 1.0f; bool arpOn = true;
uint32_t progBar0 = 1;                                              // compás de SONIDOS en que arrancó la progresión en curso
int progBanco = 0;
const Acorde *acordeAct = &FM7;
int acordeIdx = 0;

// ==============================================================================================================================================
// VOCES — BAJO
// ==============================================================================================================================================
int bPat = 0, bTipo = 0, bOct = 0; bool bSlideOn = true;
uint32_t bPh = 0, bInc = 0, bIncT = 0; int bMip = 1;
float bAmpE = 0, bFenv = 0, bFDec = 0.9998f, bSlideK = 0.0004f, bAcentoG = 1.0f; bool bGate = false; int bGateFin = -1; bool bSlideAhora = false;
SvfS bF1, bF2; Rampa bR1, bR2;
float bCutBase = 200.0f, bEnvMod = 4.0f, bK1 = 0.3f, bComp = 1.0f, bDrive = 1.6f;

// ==============================================================================================================================================
// VOCES — RITMO
// ==============================================================================================================================================
int rPat = 0; bool rPerc = true, rKickOn = true;
uint32_t kPh = 0; float kAmp = 0, kPit = 0, kClk = 0, kDecC = 0.9999f, kF0 = 150.0f, kDrive = 1.5f, kPrev = 0, duck = 0;
float clAmp = 0, clDec = 0.9996f, clTimer = 0; int clBursts = 0; SvfS clF; Coef clK;
float hAmp = 0, hDec = 0.995f, hGL = 1, hGR = 1; SvfS hF; Coef hK;
uint32_t pgPh = 0; float pgAmp = 0, pgPit = 0, pgF = 220.0f, pgDec = 0.9996f;
float shAmp = 0, shDec = 0.998f, shGL = 1, shGR = 1; SvfS shF; Coef shK;
bool fillOn = false; uint32_t fillIni = 0, fillFin = 0;

// ==============================================================================================================================================
// VOCES — SONIDOS
// ==============================================================================================================================================
// PAD: 3 notas de sierra limitada, mono, con filtro y ataque lento
uint32_t pdPh[3] = {0, 0x30000000u, 0x60000000u}, pdInc[3] = {0, 0, 0}, pdIncT[3] = {0, 0, 0}; int pdMip[3] = {1, 1, 1};
float pdAmp = 0; SvfS pdF; Rampa pdR; bool padOn = true;
// ARPEGIO
int arpDir = 0, arpDivZ = 0, arpDiv = 6; uint32_t arpN = 0;
uint32_t apPh = 0, apInc = 0; int apMip = 2; float apAmp = 0, apDec = 0.9996f; SvfS apF; Rampa apR;
// TEXTURA: riser (ruido en barrido + tono ascendente) e IMPACTO
bool texOn = false, texPrev = false;
SvfS risF; Coef risK; uint32_t risPh = 0, risInc = 0; float risAmp = 0, risProg = 0;
uint32_t impPh = 0; float impAmp = 0, impPit = 0, impNz = 0; SvfS impF; Coef impK;

// ==============================================================================================================================================
// RACK — eco ping-pong (corchea con puntillo = 18 ticks) y placa de Dattorro
// ==============================================================================================================================================
#define ECO_N 18500                                                  // 18 ticks al tempo más lento (110 BPM)
int16_t ecoL[ECO_N], ecoR[ECO_N];
int ecoW = 0, ecoDA = 15000, ecoDB = 15000; float ecoX = 0;
const float ECO_XF = 1.0f / (0.040f * SAMPLE_RATE);
float ecoLpL = 0, ecoLpR = 0, eHpL = 0, eHpR = 0; const float ECO_HP = 0.0128f;
float ecoFb = 0.45f;

float ia1[142], ia2[107], ia3[379], ia4[277];
int ii1 = 0, ii2 = 0, ii3 = 0, ii4 = 0;
float lAp1[672 + 40], lD1[4453], lAp2[1800], lD2[3720];
float rAp1[908 + 40], rD1[4217], rAp2[2656], rD2[3163];
int lAp1w = 0, lD1w = 0, lAp2w = 0, lD2w = 0, rAp1w = 0, rD1w = 0, rAp2w = 0, rD2w = 0;
float lpL = 0, lpR = 0, bwIn = 0, fbL = 0, fbR = 0, xAcc = 0, pLo = 0, pRo = 0, pLn = 0, pRn = 0;
uint32_t pFase = 0;
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
float dcXL = 0, dcYL = 0, dcXR = 0, dcYR = 0;
const float DC_R = 0.9986f;
float vB = 0.5f, vR = 0.5f, vS = 0.5f, vT = 0.5f, vEsp = 0.3f;
float mgB = 1, mgR = 1, mgS = 1;                                    // ganancias de silenciado con fundido (suaves, de varios cientos de ms)
float kFin = 55.0f;                                                 // nota final del bombo: la fundamental del acorde (bombo y bajo afinados entre sí)
float lpOL = 0, lpOR = 0;                                           // techo de agudos del master
float cmpEnv = 0, cmpGan = 1, cmpGT = 1;
uint32_t aBTipoSeen = 0, aBOctSeen = 0, aRFillSeen = 0;
bool benchForce = false;
static i2s_chan_handle_t tx_chan;

void resetAudio() {
  memset(ia1, 0, sizeof(ia1)); memset(ia2, 0, sizeof(ia2)); memset(ia3, 0, sizeof(ia3)); memset(ia4, 0, sizeof(ia4));
  memset(lAp1, 0, sizeof(lAp1)); memset(lD1, 0, sizeof(lD1)); memset(lAp2, 0, sizeof(lAp2)); memset(lD2, 0, sizeof(lD2));
  memset(rAp1, 0, sizeof(rAp1)); memset(rD1, 0, sizeof(rD1)); memset(rAp2, 0, sizeof(rAp2)); memset(rD2, 0, sizeof(rD2));
  memset(ecoL, 0, sizeof(ecoL)); memset(ecoR, 0, sizeof(ecoR));
  lpL = lpR = bwIn = fbL = fbR = xAcc = pLo = pRo = pLn = pRn = 0; pFase = 0; ecoLpL = ecoLpR = eHpL = eHpR = 0;
  bF1 = bF2 = clF = hF = shF = pdF = apF = risF = impF = SvfS{0, 0};
  dcXL = dcYL = dcXR = dcYR = 0; lpOL = lpOR = 0; cmpEnv = 0; cmpGan = 1; ecoW = 0;
}

IRAM_ATTR void fijarTempo(int capa, int z) {
  float tl = SR * 60.0f / (bpmDe(z) * (float)TPB);
  if (capa == 0) { zB = z; tlB = tl; } else if (capa == 1) { zR = z; tlR = tl; } else { zS = z; tlS = tl; }
}
inline int zonaDe(float p, int z) {                                    // zona de tempo desde la perilla, con histéresis
  float zz = p * (float)NTEMPO; int c = (int)zz; if (c > NTEMPO - 1) c = NTEMPO - 1;
  if (c != z && fabsf(zz - ((float)z + 0.5f)) < 0.62f) c = z;
  return c;
}
IRAM_ATTR void ajustarEco() {                                         // el eco sigue el tempo de SONIDOS; sólo reacomoda con el crossfade anterior terminado
  int nuevo = (int)(18.0f * tlS);
  if (nuevo > ECO_N - 4) nuevo = ECO_N - 4;
  if (ecoX <= 0.0f && abs(nuevo - ecoDA) > 24) { ecoDB = ecoDA; ecoDA = nuevo; ecoX = 1.0f; }
}

// ─── armonía: el acorde en curso (lo manda SONIDOS) rige al bajo, al pad, al arpegio y al bombo ───
IRAM_ATTR void aplicarAcorde() {
  const Acorde &a = *acordeAct;
  float root = 110.0f * powf(2.0f, (float)a.raiz / 12.0f);            // La2 = 110 Hz
  float n[3] = { root, root * 1.4983f, root * powf(2.0f, (float)(a.ter + 12) / 12.0f) };    // fundamental, quinta, tercera una octava arriba
  for (int k = 0; k < 3; k++) { pdIncT[k] = incF(n[k]); pdMip[k] = mipDe(n[k]); }
  { float fk = root * 0.5f; while (fk < 41.0f) fk *= 2.0f; while (fk >= 82.0f) fk *= 0.5f; kFin = fk; }   // el bombo cae sobre la fundamental del acorde
}
IRAM_ATTR void elegirAcorde() {
  int bars = PROG_BARS[progBanco];
  int idx = (int)((barS - progBar0) / (uint32_t)bars) % PROG_N[progBanco];
  if (idx != acordeIdx || acordeAct != PROG_A[progBanco][idx]) { acordeIdx = idx; acordeAct = PROG_A[progBanco][idx]; aplicarAcorde(); }
}

// ─── disparadores (aquí sí se pueden usar expf/powf: ocurren pocas veces por segundo) ───
IRAM_ATTR void dispararKick() {
  kAmp = 1.0f; kPit = 1.0f; kClk = 0.6f; kPh = 0; duck = 1.0f;
}
IRAM_ATTR void dispararClap() { clBursts = 3; clTimer = 0; clDec = 0.9988f; }
IRAM_ATTR void dispararHat(float v, bool abierto, float tau) {
  hAmp = v; hDec = expf(-1.0f / (tau * (abierto ? 7.0f : 1.0f) * SR));
  float p = rnd01() - 0.5f; hGL = 1.0f - 0.40f * p; hGR = 1.0f + 0.40f * p;
}
IRAM_ATTR void dispararConga(bool aguda, float v) {
  pgAmp = v; pgPit = 1.0f; pgPh = 0; pgF = aguda ? 330.0f : 215.0f; pgDec = expf(-1.0f / (0.085f * SR));
}
IRAM_ATTR void dispararShaker(float v) { shAmp = v; shDec = expf(-1.0f / (0.022f * SR)); float p = rnd01() - 0.5f; shGL = 1.0f - 0.8f * p; shGR = 1.0f + 0.8f * p; }
IRAM_ATTR void dispararImpacto() { impAmp = 1.0f; impPit = 1.0f; impNz = 1.0f; impPh = 0; duck = 1.0f; }

// intervalo en semitonos de cada letra del patrón de bajo, según el acorde
IRAM_ATTR int intervaloBajo(char c) {
  switch (c | 0x20) {
    case 'r': return 0;
    case 'w': return 1;
    case 't': return acordeAct->ter;
    case 'f': return 7;
    case 's': return acordeAct->sep - 12 + 12;
    case 'o': return 12;
    default:  return 0;
  }
}
IRAM_ATTR void dispararBajo(char c, bool slide, bool legatoSig) {
  bool acento = (c >= 'A' && c <= 'Z');
  int semi = acordeAct->raiz + intervaloBajo(c) + 12 * bOct + (bVarOct ? 12 : 0);
  float f = 55.0f * powf(2.0f, (float)semi / 12.0f);
  bIncT = incF(f); bMip = mipDe(f);
  bSlideAhora = slide && bSlideOn && bGate;
  if (!bSlideAhora) bInc = bIncT;
  bGate = true;
  bAcentoG = acento ? 1.3f : 1.0f;
  bFenv = acento ? 1.25f : 1.0f;
  float step = (float)TPS * tlB;
  bGateFin = (int)(step * (legatoSig ? 1.08f : 0.40f + 0.65f * (float)pLargo));
  // el filtro arranca ya abierto (sin esperar la rampa)
  float cut = bCutBase * (1.0f + bEnvMod * bFenv); if (cut > 5200.0f) cut = 5200.0f;
  float g = gDe(cut);
  bR1.cur = coefDe(g, bK1); bR2.cur = coefDe(g, 1.4142f);
  bR1.paso = bR2.paso = Coef{0, 0, 0, 0};
}

IRAM_ATTR void pasoBajo16(uint32_t s16) {
  int s = (int)(s16 & 15);
  char c = BAJO_N[bPat][s];
  if (c != '-' && !((mBSkip >> s) & 1)) {
    bool slide = (BAJO_S[bPat][s] == '/');
    char sig = BAJO_N[bPat][(s + 1) & 15]; bool legato = (BAJO_S[bPat][(s + 1) & 15] == '/') && sig != '-';
    bVarOct = (mBOct >> s) & 1;
    dispararBajo(c, slide, legato);
  }
}
IRAM_ATTR void pasoRitmo16(uint32_t s16) {
  int s = (int)(s16 & 15);
  if (fillOn) {                                                       // el relleno reemplaza a clap, hats y percusión: redoble de congas con crescendo
    float prog = (float)(tickR - fillIni) / (float)(fillFin - fillIni);
    dispararConga((s & 1) == 0, 0.35f + 0.65f * prog);
    if (prog > 0.7f) dispararClap();
    if (RIT_K[rPat][s] == 'x') dispararKick();
  } else {
    if (RIT_K[rPat][s] == 'x') dispararKick();
    if (RIT_C[rPat][s] == 'x') dispararClap();
    char h = RIT_H[rPat][s];
    if ((mHSkip >> s) & 1) h = '-'; else if (h == '-' && ((mHAdd >> s) & 1)) h = 'x';
    if (h != '-') {
      float tau = 0.012f + 0.050f * ((float)pHats);                  // POT4: de seco a abierto
      dispararHat(h == 'X' ? 0.95f : 0.62f, h == 'o', tau);
    }
    if (rPerc) {
      char p = RIT_P[rPat][s]; if (p == '-' && ((mPAdd >> s) & 1)) p = (s & 2) ? 'a' : 'b';
      if (p == 'a') dispararConga(true, 0.7f * percK); else if (p == 'b') dispararConga(false, 0.8f * percK);
      char sh = RIT_S[rPat][s]; if (sh == 's') dispararShaker(0.5f * percK); else if (sh == 'S') dispararShaker(0.9f * percK);
    }
  }
}

IRAM_ATTR void pasoArp(uint32_t n) {                                  // n: contador de pasos del arpegio
  const Acorde &a = *acordeAct;
  int iv[4] = { 0, a.ter, 7, a.sep };
  int idx;
  switch (arpDir) {
    case 0: idx = (int)(n % 8); break;                                // sube
    case 1: idx = 7 - (int)(n % 8); break;                            // baja
    case 2: { int m = (int)(n % 14); idx = m < 8 ? m : 14 - m; } break;   // sube y baja
    default: idx = (int)(rnd01() * 7.99f); break;                     // azar
  }
  int semi = a.raiz + iv[idx & 3] + 12 * (idx >> 2);
  float f = 220.0f * powf(2.0f, (float)semi / 12.0f);                 // arpegio desde La3
  apInc = incF(f); apMip = mipDe(f);
  apAmp = ((n & 3) == 0) ? 1.0f : 0.72f; apPh = 0;
  apDec = expf(-1.0f / (0.11f * SR));
}

// ─── cuantización: patrones en el siguiente tiempo de SU capa, progresión en el siguiente compás de SONIDOS ───
uint32_t sTogB = 0, sTogR = 0, sTogS = 0, pBPatSeen = 0, pRPatSeen = 0, pADirSeen = 0, pProgSeen = 0;
IRAM_ATTR void generarVarBajo() {                                     // EVOLUCIÓN: el 1.er compás de cada frase de 4 sale limpio y la variación crece hasta el 4.º
  int pos = (int)((barB - 1) & 3);
  float f = pEvo * (pos == 0 ? 0.0f : (pos == 3 ? 1.0f : 0.55f));
  mBSkip = mBOct = 0;
  if (f <= 0.01f) return;
  for (int s = 0; s < 16; s++) {
    char c = BAJO_N[bPat][s];
    if (c != '-' && s != 0 && rnd01() < 0.30f * f) mBSkip |= (uint16_t)(1u << s);
    if (c != '-' && rnd01() < 0.28f * f) mBOct |= (uint16_t)(1u << s);
  }
}
IRAM_ATTR void generarVarRitmo() {
  int pos = (int)((barR - 1) & 3);
  float f = pEvo * (pos == 0 ? 0.0f : (pos == 3 ? 1.0f : 0.55f));
  mHSkip = mHAdd = mPAdd = 0;
  if (f <= 0.01f) return;
  for (int s = 0; s < 16; s++) {
    char h = RIT_H[rPat][s];
    if (h != '-' && h != 'X' && rnd01() < 0.25f * f) mHSkip |= (uint16_t)(1u << s);
    if (h == '-' && (s & 1) && rnd01() < 0.22f * f) mHAdd |= (uint16_t)(1u << s);
    if (RIT_P[rPat][s] == '-' && rnd01() < 0.18f * f) mPAdd |= (uint16_t)(1u << s);
  }
}
IRAM_ATTR void activarPendientes(int ref, uint32_t t) {                // al empezar un compás de la capa ref, las capas que esperaban entran justo ahí (en fase)
  if (pendB && ref != 0) { pendB = false; runB = true; tickB = t; accB = tlB; }
  if (pendR && ref != 1) { pendR = false; runR = true; tickR = t; accR = tlR; }
  if (pendS && ref != 2) { pendS = false; runS = true; tickS = t; accS = tlS; }
}
IRAM_ATTR void pasoBajo() {
  if ((tickB % TPB) == 0) { uint32_t v = reqBPat; if (v != pBPatSeen) { pBPatSeen = v; bPat = (int)(v % NBAJOPAT); } }
  if ((tickB % TPC) == 0) { barB++; generarVarBajo(); activarPendientes(0, tickB); }
  if ((tickB % TPS) == 0) pasoBajo16(tickB / TPS);
}
IRAM_ATTR void pasoRitmo() {
  if ((tickR % TPB) == 0) { uint32_t v = reqRPat; if (v != pRPatSeen) { pRPatSeen = v; rPat = (int)(v % NRITMO); } }
  if ((tickR % TPC) == 0) { barR++; generarVarRitmo(); activarPendientes(1, tickR); }
  if (!fillOn && (tickR % TPC) == 3 * TPB && ((barR - 1) & 3) == 3 && pEvo > 0.55f && rnd01() < (pEvo - 0.5f) * 1.6f) {   // al final de la frase, a veces un relleno
    fillIni = tickR; fillFin = tickR + TPB; fillOn = true;
  }
  if ((tickR % TPS) == 0) pasoRitmo16(tickR / TPS);
  if (fillOn && tickR >= fillFin) fillOn = false;
}
IRAM_ATTR void pasoSonidos() {
  if ((tickS % TPB) == 0) { uint32_t v = reqADir; if (v != pADirSeen) { pADirSeen = v; arpDir = (int)(v % 5); arpN = 0; } }
  if ((tickS % TPC) == 0) {
    barS++;
    uint32_t v = reqProg; if (v != pProgSeen) { pProgSeen = v; progBanco = (int)(v & 3); progBar0 = barS; }
    elegirAcorde(); activarPendientes(2, tickS);
  }
  if (arpDir != 4 && (tickS % (uint32_t)arpDiv) == 0) pasoArp(tickS / (uint32_t)arpDiv);
}

// ==============================================================================================================================================
// CONTROL (core 0): recogida de pots por capa, botones por capa, LEDs
// ==============================================================================================================================================
float readPot(uint8_t pin) { uint32_t s = 0; for (int i = 0; i < 4; i++) s += analogRead(pin); return (float)(s >> 2) / 4095.0f; }

float flashLevel = 0.0f;
float meterV = 0.0f; unsigned long meterT = 0; uint8_t meterHue = 0;
static const uint8_t HUE_CAPA[3] = { 0, 160, 96 };                  // BAJO rojo · RITMO azul · SONIDOS verde
void renderLEDs() {
  static unsigned long lastFrame = 0;
  unsigned long t = millis();
  if (t - lastFrame < LED_REFRESH_MS) return;
  lastFrame = t;
  int capa = capaCtl;
  float pul = g_pul[capa];
  bool medidor = (meterT != 0 && t - meterT < 900);
  if (medidor) {                                                      // al mover una perilla los LED 1-5 muestran su valor
    float n = meterV * 5.0f;
    for (int i = 0; i < 5; i++) { float l = n - (float)i; if (l < 0) l = 0; if (l > 1) l = 1; leds[i] = CHSV(meterHue, 255, (uint8_t)(6 + l * 240.0f)); }
  } else if (capa == 0) {
    leds[0] = CHSV(0,   255, (uint8_t)(50 + pul * 190.0f));
    leds[1] = CHSV(0,   255, (uint8_t)(20 + g_bajo * 235.0f));
    leds[2] = g_tipo == 0 ? CRGB(255, 40, 0) : CRGB(0, 80, 255);                 // ácido: naranja · sub: azul
    leds[3] = CHSV((uint8_t)(g_bPat * 32), 255, 70);                              // patrón: un color por línea
    leds[4] = CHSV(0, 0, (uint8_t)(g_oct == 0 ? 25 : (g_oct > 0 ? 200 : 6)));    // octava: normal tenue · arriba brillante · abajo casi apagada
  } else if (capa == 1) {
    leds[0] = CHSV(160, 255, (uint8_t)(50 + pul * 190.0f));
    leds[1] = CHSV(0,   255, (uint8_t)(20 + g_kick * 235.0f));
    leds[2] = CHSV(40,  255, (uint8_t)(20 + g_hat * 235.0f));
    leds[3] = g_fill ? CRGB(255, 255, 255) : CHSV((uint8_t)(g_rPat * 32), 255, 40);   // groove: un color por patrón (blanco = relleno)
    leds[4] = g_perc ? CRGB(255, 120, 0) : CRGB(3, 1, 0);
  } else {
    leds[0] = CHSV(96, 255, (uint8_t)(50 + pul * 190.0f));
    leds[1] = CHSV(120, 255, (uint8_t)(20 + g_arp * 235.0f));
    leds[2] = CHSV(170, 255, (uint8_t)(25 + g_pad * 220.0f));
    leds[3] = CHSV((uint8_t)(g_prog * 60), 255, 90);                              // progresión: un color por banco
    leds[4] = CHSV(215, 255, (uint8_t)(4 + g_tex * 250.0f));
  }
  if (!medidor && g_estado[capa] == 0) { leds[0] = CRGB(40, 0, 0); }            // esta capa está en STOP
  if (!medidor && g_estado[capa] == 2) { leds[0] = ((t / 150) & 1) ? CRGB(120, 60, 0) : CRGB(0, 0, 0); }   // esperando el compás para entrar
  if (g_sinRecoger) leds[5] = CRGB(255, 120, 0);
  else leds[5] = CRGB(8, 8, 8);
  if (xrunAt != 0 && millis() - xrunAt < 1500) leds[5] = CRGB(255, 0, 0);
  onboard[0] = CHSV(HUE_CAPA[capa], 255, 20 + (uint8_t)(g_kick * 120.0f + g_bajo * 100.0f));
  onboard[0].nscale8(ONBOARD_BRIGHT);
  if (flashLevel > 0.02f) { uint8_t w = (uint8_t)(flashLevel * 130.0f); for (int i = 0; i < NUM_LEDS; i++) leds[i] += CRGB(w, w, w); flashLevel *= 0.62f; }
  FastLED.show();
}

// Cada capa guarda sus 4 valores. Al volver a una capa las perillas entran en ESCALADO DE VALOR (value scaling): la perilla
// recorre su camino físico y el valor guardado se estira para encontrarse con ella en los extremos — nada salta, y la perilla
// responde desde el primer milímetro. Cuando coinciden (a menos de 3 %) pasan a mandar directo.
float guardado[NCAPAS][4] = { { 0.75f, P_TEMPO_INI, 0.45f, 0.55f }, { 0.75f, P_TEMPO_INI, 0.5f, 0.5f }, { 0.75f, P_TEMPO_INI, 0.5f, 0.35f } };
bool  primeraLectura[4] = { true, true, true, true };
bool  bloqueado[4] = { true, true, true, true };                    // true = la perilla manda directo
bool  entrada[4] = { false, false, false, false };                  // true = falta registrar dónde estaba la perilla al entrar a la capa
float p0[4] = { 0, 0, 0, 0 };
float potUlt[4] = { -1, -1, -1, -1 };
void escribirParam(int capa, int p, float v) {                      // POT1 = volumen · POT2 = tempo de la capa · POT3/POT4 = sonido de la capa
  if (capa == 0)      { if (p == 0) pVolB = v; else if (p == 1) pTempoB = v; else if (p == 2) pCorte = v; else pAcid = v; }
  else if (capa == 1) { if (p == 0) pVolR = v; else if (p == 1) pTempoR = v; else if (p == 2) pPunch = v; else pHats = v; }
  else                { if (p == 0) pVolS = v; else if (p == 1) pTempoS = v; else if (p == 2) pBrillo = v; else pEspacio = v; }
}
unsigned long capaT = 0;                                            // cuándo se cambió de capa (la pantalla lo anuncia en grande)
char toastBuf[24] = ""; unsigned long toastT = 0;                   // último gesto, en la línea de abajo de la pantalla
void toast(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(toastBuf, sizeof(toastBuf), fmt, ap); va_end(ap); toastT = millis(); }
void cambiarCapa(int n) {
  for (int q = 0; q < 4; q++) { bloqueado[q] = false; entrada[q] = true; }
  capaCtl = n; flashLevel = 0.9f; capaT = millis(); toastT = 0;
}
void leerPotCapa(int p, float v) {
  int capa = capaCtl;
  float out = v;
  if (primeraLectura[p]) {                                            // al encender, la capa 1 toma la perilla (y el tempo se copia a las tres capas para que arranquen en fase)
    primeraLectura[p] = false; guardado[0][p] = v; bloqueado[p] = true; escribirParam(0, p, v);
    if (p == 1) { guardado[1][1] = guardado[2][1] = v; pTempoR = v; pTempoS = v; }
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
      meterV = out; meterT = millis(); meterHue = HUE_CAPA[capa];
    }
  }
  bool alguno = false;
  for (int q = 0; q < 4; q++) if (!bloqueado[q]) alguno = true;
  g_sinRecoger = alguno;
}

// ==============================================================================================================================================
// PANTALLA OLED (SSD1306 128x64, I2C: SDA 21 / SCL 38). Driver propio sin librerías, en una tarea de baja prioridad en el núcleo 0:
// nunca toca el audio (núcleo 1) ni retrasa a los botones (la tarea de control tiene más prioridad). Si no hay pantalla, la tarea se borra.
// Muestra la capa en grande al cambiar, y después el MAPA: nombre y valor de cada perilla + función de cada botón.
// ==============================================================================================================================================
uint8_t oledAddr = 0x3C; volatile int g_oled = 0;
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
  Wire.begin(21, 38); Wire.setClock(400000); Wire.setTimeOut(5);
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
    Wire.beginTransmission(oledAddr); Wire.write((uint8_t)0x00); Wire.write(cmd, 6); if (Wire.endTransmission() != 0) return false;
    for (int i = 0; i < 128; i += 64) { Wire.beginTransmission(oledAddr); Wire.write((uint8_t)0x40); Wire.write(&fb[pg * 128 + i], 64); if (Wire.endTransmission() != 0) return false; }
    memcpy(&fbPrev[pg * 128], &fb[pg * 128], 128);
    vTaskDelay(1);
  }
  return true;
}
static const char *CAPA_NOM[NCAPAS] = { "BAJO", "RITMO", "SONIDOS" };
static const char *POT_NOM[NCAPAS][4] = { { "VOLUMEN", "TEMPO", "CORTE", "ACIDO" }, { "VOLUMEN", "TEMPO", "PUNCH", "HATS" }, { "VOLUMEN", "TEMPO", "BRILLO", "ESPACIO" } };
static const char *BTN_NOM[NCAPAS][3] = { { "PATRON", "TIPO", "OCTAVA" }, { "GROOVE", "RELLENO", "PERC" }, { "PROGR.", "TEXTURA", "ARPEG." } };
static const char *EST_NOM[3] = { "STOP", "PLAY", "ESPERA" };
void oDibujar() {
  memset(fb, 0, sizeof(fb));
  unsigned long t = millis();
  int capa = capaCtl; char b[32];
  int est = g_estado[capa]; int bpm = 110 + g_zona[capa];
  if (t - capaT < 1100 && capaT != 0) {                               // anuncio de capa
    snprintf(b, sizeof(b), "CAPA %d DE %d", capa + 1, NCAPAS); oTextC(4, b, 2);
    oTextC(26, CAPA_NOM[capa], 3);
    oRect(0, 52, 128, 1, true);
    snprintf(b, sizeof(b), "%s  %d BPM", EST_NOM[est], bpm); oTextC(55, b, 1);
    return;
  }
  oRect(0, 0, 128, 9, true);                                          // mapa de la capa
  snprintf(b, sizeof(b), "%d/%d %s", capa + 1, NCAPAS, CAPA_NOM[capa]); oText(2, 1, b, 1, true);
  snprintf(b, sizeof(b), "%s", EST_NOM[est]); oText(128 - 2 - (int)strlen(b) * 6, 1, b, 1, true);
  for (int p = 0; p < 4; p++) {
    int y = 11 + p * 9; float v = guardado[capa][p];
    snprintf(b, sizeof(b), "P%d %s", p + 1, POT_NOM[capa][p]); oText(0, y, b);
    oRect(54, y + 1, 46, 6, true); oRect(55, y + 2, 44, 4, false);
    oRect(55, y + 2, (int)(44.0f * v + 0.5f), 4, true);
    if (!bloqueado[p]) { int xk = 55 + (int)(43.0f * potUlt[p]); oRect(xk, y - 1, 1, 9, true); }   // perilla aún sin alcanzar el valor: marca donde está
    if (p == 1) snprintf(b, sizeof(b), "%d", bpm);
    else snprintf(b, sizeof(b), "%d", (int)(v * 100.0f + 0.5f));
    oText(128 - (int)strlen(b) * 6, y, b);
  }
  snprintf(b, sizeof(b), "B2 %-7s B3 %-7s", est ? "STOP" : "PLAY", BTN_NOM[capa][0]); oText(0, 47, b);
  snprintf(b, sizeof(b), "B4 %-7s B5 %-7s", BTN_NOM[capa][1], BTN_NOM[capa][2]); oText(0, 55, b);
  if (toastT != 0 && t - toastT < 1500) { oRect(0, 54, 128, 10, true); oText(2, 56, toastBuf, 1, true); }   // último gesto sobre la fila de abajo
}
void oledTask(void *) {
  bool hay = oInit();
#if DIAG
  g_oled = hay ? oledAddr : -1;
#endif
  if (!hay) vTaskDelete(NULL);
  memset(fbPrev, 0xA5, sizeof(fbPrev));                               // fuerza el primer dibujo completo
  int fallos = 0;
  for (;;) {
    oDibujar();
#if DIAG
    unsigned long t0 = millis();
#endif
    bool ok = oFlush();
#if DIAG
    { unsigned long d = millis() - t0; if (d > dgOledMax) dgOledMax = d; }
#endif
    if (ok) fallos = 0;
    else if (++fallos >= 3) { fallos = 0; vTaskDelay(3000 / portTICK_PERIOD_MS); memset(fbPrev, 0xA5, sizeof(fbPrev)); oInit(); }   // si la pantalla no contesta, se aparta 3 s y se reinicia
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

bool bL[5] = {HIGH, HIGH, HIGH, HIGH, HIGH}; unsigned long bT[5] = {0, 0, 0, 0, 0};
void pasoControl() {
  unsigned long t = millis();
  bool b[5];
#if DIAG
  b[0] = HIGH;                                                        // GPIO 44 es el RX de Serial0
#else
  b[0] = digitalRead(BTN1_PIN);
#endif
  b[1] = digitalRead(BTN2_PIN); b[2] = digitalRead(BTN3_PIN); b[3] = digitalRead(BTN4_PIN); b[4] = digitalRead(BTN5_PIN);
  bool f[5];
  for (int i = 0; i < 5; i++) { f[i] = (b[i] == LOW && bL[i] == HIGH && t - bT[i] > DEBOUNCE_MS); if (f[i]) bT[i] = t; }
  // REGLAS FIJAS: BTN1 = capa siguiente · BTN2 = STOP/PLAY de ESTA capa · POT1 = volumen · POT2 = tempo de ESTA capa
  if (f[0]) cambiarCapa((capaCtl + 1) % NCAPAS);
  int capa = capaCtl;
  if (f[1]) {
    bool parar = (g_estado[capa] != 0);
    if (capa == 0) reqTogB = reqTogB + 1; else if (capa == 1) reqTogR = reqTogR + 1; else reqTogS = reqTogS + 1;
    flashLevel = 0.6f;
    if (parar) toast("%s STOP", CAPA_NOM[capa]); else toast("%s PLAY (ENTRA AL COMPAS)", CAPA_NOM[capa]);
  }
  if (capa == 0) {                                                    // BAJO
    if (f[2]) { reqBPat  = reqBPat  + 1; flashLevel = 0.6f; toast("PATRON %s", BAJO_NOM[reqBPat % NBAJOPAT]); }
    if (f[3]) { reqBTipo = reqBTipo + 1; flashLevel = 0.6f; toast("TIPO %s", (reqBTipo & 1) ? "SUB" : "ACIDO"); }
    if (f[4]) { reqBOct  = reqBOct  + 1; flashLevel = 0.5f; int m = (int)(reqBOct % 3); toast("OCTAVA %s", m == 0 ? "NORMAL" : (m == 1 ? "+1" : "-1")); }
    reqTex = false;
  } else if (capa == 1) {                                             // RITMO
    if (f[2]) { reqRPat  = reqRPat  + 1; flashLevel = 0.6f; toast("GROOVE %s", RIT_NOM[reqRPat % NRITMO]); }
    if (f[3]) { reqRFill = reqRFill + 1; flashLevel = 0.8f; toast("RELLENO"); }
    if (f[4]) { reqRPerc = !reqRPerc;    flashLevel = 0.5f; toast("PERCUSION %s", reqRPerc ? "SI" : "NO"); }
    reqTex = false;
  } else {                                                            // SONIDOS
    if (f[2]) { reqProg  = reqProg  + 1; flashLevel = 0.9f; toast("PROGRESION %s", PROG_NOM[reqProg & 3]); }
    reqTex = (b[3] == LOW);                                           // textura: mientras se mantiene; al soltar, IMPACTO
    if (f[3]) toast("TEXTURA: SUELTA = IMPACTO");
    if (f[4]) { reqADir  = reqADir  + 1; flashLevel = 0.5f; static const char *DN[5] = { "SUBE", "BAJA", "SUBE-BAJA", "AZAR", "APAGADO" }; toast("ARPEGIO %s", DN[reqADir % 5]); }
  }
  for (int i = 0; i < 5; i++) bL[i] = b[i];
  static const uint8_t POT_PIN[4] = { POT1, POT2, POT3, POT4 };
  static uint8_t scan = 0;
  int pi = scan; scan = (scan + 1) & 3;
  float v = readPot(POT_PIN[pi]);
  if (potUlt[pi] < 0.0f || fabsf(v - potUlt[pi]) > 0.004f) potUlt[pi] = v;     // banda muerta: el ruido del ADC no mueve nada
  leerPotCapa(pi, potUlt[pi]);
  renderLEDs();
#if DIAG
  static unsigned long lastP = 0;
  if (t - lastP > 2000) {
    lastP = t;
    Serial0.printf("DIAG render promedio %u us (%.0f %% del buffer)  maximo %u us (%.0f %%)  retrasos %u\n",
                   (unsigned)g_usAvg, 100.0f * g_usAvg / BUF_US, (unsigned)g_usMax, 100.0f * g_usMax / BUF_US, (unsigned)g_xruns);
    g_usMax = 0;
    Serial0.printf("OLED: %d (0x3C=60, 0x3D=61, -1=no responde)\n", (int)g_oled);
    Serial0.printf("NIVEL pico antes del limitador %.2f | muestras sobre 0.75: %.3f %% | compresor activo (>1 dB): %.1f %%\n", dgPk, dgN ? 100.0f * dgSobre / dgN : 0.0f, dgN ? 100.0f * dgComp / dgN : 0.0f);
    dgPk = 0; dgN = dgSobre = dgComp = 0;
    if (ccN) { Serial0.printf("DIAG ciclos/muestra: reloj %u | bajo %u | kick+clap %u | hats+perc %u | pad %u | arpegio %u | eco+bus %u | textura %u | placa %u | salida %u\n",
      (unsigned)(cc[0]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[1]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[2]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[3]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[4]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[5]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[6]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[7]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[8]/(ccN*BUFFER_SAMPLES)),(unsigned)(cc[9]/(ccN*BUFFER_SAMPLES)));
      for (int q = 0; q < 10; q++) cc[q] = 0; ccN = 0; }
  }
#endif
}

// ==============================================================================================================================================
// AUDIO
// ==============================================================================================================================================
IRAM_ATTR void renderCore(int16_t *buffer) {
  // ── pedidos de la tarea de control ──
  { uint32_t v = reqBTipo; if (v != aBTipoSeen) { aBTipoSeen = v; bTipo = (int)(v & 1); } }
  { uint32_t v = reqBOct;  if (v != aBOctSeen)  { aBOctSeen = v;  int m = (int)(v % 3); bOct = (m == 0) ? 0 : (m == 1 ? 1 : -1); } }
  { uint32_t v = reqRFill; if (v != aRFillSeen) { aRFillSeen = v;
      if (runR) { fillIni = tickR; uint32_t fin = ((tickR / TPC) + 1) * TPC; if (fin - tickR < TPB) fin += TPC; fillFin = fin; fillOn = true; } } }
  // STOP/PLAY por capa: parar es inmediato (con fundido); volver a entrar espera el inicio de compás de una capa que sí suene, para quedar en fase
  { uint32_t d = reqTogB; if (d != sTogB) { if ((d - sTogB) & 1) { if (runB || pendB) { runB = pendB = false; bGate = false; } else pendB = true; } sTogB = d; } }
  { uint32_t d = reqTogR; if (d != sTogR) { if ((d - sTogR) & 1) { if (runR || pendR) { runR = pendR = false; fillOn = false; } else pendR = true; } sTogR = d; } }
  { uint32_t d = reqTogS; if (d != sTogS) { if ((d - sTogS) & 1) { if (runS || pendS) { runS = pendS = false; } else pendS = true; } sTogS = d; } }
  if (!runB && !runR && !runS) {                                       // si no suena nada, lo que se pida arranca ya, junto y en el compás 1
    if (pendB) { pendB = false; runB = true; tickB = 0; accB = tlB; }
    if (pendR) { pendR = false; runR = true; tickR = 0; accR = tlR; }
    if (pendS) { pendS = false; runS = true; tickS = 0; accS = tlS; }
  }
  if (benchForce || DIAG) { runB = runR = runS = true; }
  rPerc = reqRPerc; rKickOn = reqRKick; arpOn = true; bSlideOn = true;
  percK = 2.0f * pPercV; arpK = 2.0f * pArpV;
  texOn = reqTex || benchForce || DIAG; padOn = runS || benchForce || DIAG;
  if (texPrev && !texOn) dispararImpacto();                           // soltar la textura = IMPACTO
  texPrev = texOn;
  if (benchForce || DIAG) { rPerc = true; rKickOn = true; }

  // tempo propio de cada capa (41 zonas de 1 BPM con histéresis)
  { int c = zonaDe(pTempoB, zB); if (c != zB) fijarTempo(0, c); }
  { int c = zonaDe(pTempoR, zR); if (c != zR) fijarTempo(1, c); }
  { int c = zonaDe(pTempoS, zS); if (c != zS) fijarTempo(2, c); }
  ajustarEco();
  { static const int DIV[4] = { 6, 12, 8, 3 }; int z = (int)(pArpDiv * 3.999f); arpDiv = DIV[z]; }

  // ── objetivos por bloque (aquí sí valen tanf/powf: se pagan 1 vez cada 128 muestras) ──
  const float inv = 1.0f / (float)BUFFER_SAMPLES;
  { float t;                                                           // fundidos de silenciado: salida ~160 ms · entrada: ritmo 60 ms, bajo 120 ms, sonidos 400 ms
    t = runB ? 1.0f : 0.0f; mgB += (t - mgB) * (t > mgB ? 0.024f : 0.018f);
    t = runR ? 1.0f : 0.0f; mgR += (t - mgR) * (t > mgR ? 0.047f : 0.018f);
    t = runS ? 1.0f : 0.0f; mgS += (t - mgS) * (t > mgS ? 0.0072f : 0.018f); }
  const float volB = pVolB * pVolB * mgB, volR = pVolR * pVolR * mgR, volS = pVolS * pVolS * mgS, volT = pVolS * pVolS;
  const float dVB = (volB - vB) * inv, dVR = (volR - vR) * inv, dVS = (volS - vS) * inv, dVT = (volT - vT) * inv, dEsp = (pEspacio - vEsp) * inv;
  bCutBase = 70.0f * powf(34.0f, pCorte);                             // 70 Hz … 2.4 kHz
  bEnvMod = 0.5f + 7.0f * pAcid;
  bK1 = 1.0f / (0.8f + 5.7f * pAcid * pAcid);                         // resonancia: Q de 0.8 a 6.5
  bComp = 1.0f / (0.55f + 0.12f * (0.8f + 5.7f * pAcid * pAcid));
  bFDec = expf(-1.0f / ((0.32f - 0.22f * pAcid) * SR));               // el filtro se cierra de 320 ms a 100 ms
  bSlideK = 1.0f / (0.060f * SR);
  kF0 = 120.0f + 90.0f * (1.0f - pPunch); kDrive = 1.0f + 1.3f * pPunch;
  kDecC = expf(-1.0f / ((0.10f + 0.20f * pPunch) * SR));
  hK = coefDe(gDe(7000.0f + 2800.0f * pHats), 1.0f);
  clK = coefDe(gDe(1150.0f), 1.0f / 1.1f);
  shK = coefDe(gDe(6200.0f), 1.0f / 0.8f);
  float cutPad = 250.0f * powf(16.0f, pBrillo);                       // 250 Hz … 4 kHz
  rampaHacia(pdR, coefDe(gDe(cutPad), 1.0f / 0.8f), inv);
  rampaHacia(apR, coefDe(gDe(cutPad * 1.6f), 1.0f / 1.3f), inv);
  if (texOn) {
    risProg += (float)BUFFER_SAMPLES / (8.0f * SR); if (risProg > 1.0f) risProg = 1.0f;
    risK = coefDe(gDe(350.0f * powf(26.0f, risProg)), 1.0f / 3.0f);
    risInc = incF(160.0f * powf(9.0f, risProg));
  } else risProg = 0.0f;
  impK = coefDe(gDe(1700.0f), 1.4142f);
  for (int k = 0; k < 3; k++) pdInc[k] += (int32_t)(pdIncT[k] - pdInc[k]) / 32;
  float acumBajo = 0, acumKick = 0, acumHat = 0, acumArp = 0, acumPad = 0;
  const float invR8 = 0.125f;

  for (int i = 0; i < BUFFER_SAMPLES; i++) {
#if DIAG
    ccT = xthal_get_ccount();
#endif
    // ── relojes: una por capa, cada una con su tempo ──
    if (runB) { accB += 1.0f; if (accB >= tlB) { accB -= tlB; pasoBajo();    tickB++; } }
    if (runR) { accR += 1.0f; if (accR >= tlR) { accR -= tlR; pasoRitmo();   tickR++; } }
    if (runS) { accS += 1.0f; if (accS >= tlS) { accS -= tlS; pasoSonidos(); tickS++; } }
    if (bGateFin >= 0 && --bGateFin < 0) bGate = false;
    vB += dVB; vR += dVR; vS += dVS; vT += dVT; vEsp += dEsp;
    const float nz = ruido();
    MARCA(0)

    // ── CAPA 1 · BAJO ──
    float bajo = 0;
    bAmpE += ((bGate ? 1.0f : 0.0f) - bAmpE) * (bGate ? 0.020f : 0.0045f);
    if (bAmpE > 0.0005f) {
      if (bTipo == 0) {                                                // ÁCIDO: sierra → dos SVF (24 dB) con la frecuencia de corte siguiendo la envolvente
        if (bSlideAhora) bInc += (int32_t)((float)(int32_t)(bIncT - bInc) * bSlideK);
        bPh += bInc;
        bFenv *= bFDec;
        if ((i & 7) == 0) {                                            // los coeficientes se recalculan cada 8 muestras y se interpolan
          float cut = bCutBase * (1.0f + bEnvMod * bFenv); if (cut > 5200.0f) cut = 5200.0f;
          float g = gDe(cut);
          rampaHacia(bR1, coefDe(g, bK1), invR8); rampaHacia(bR2, coefDe(g, 1.4142f), invR8);
        }
        rampaAvanza(bR1); rampaAvanza(bR2);
        float x = leeTabla(tabSaw[bMip], bPh) * 0.7f;
        float y = svfLP(bF2, bR2.cur, svfLP(bF1, bR1.cur, x)) * bComp;
        bajo = sat3(y * bDrive * bAcentoG * 0.75f) * 0.95f * bAmpE;
      } else {                                                         // SUB: seno con un 2º armónico suave
        if (bSlideAhora) bInc += (int32_t)((float)(int32_t)(bIncT - bInc) * bSlideK);
        bPh += bInc;
        bajo = tabPh(SUBW, bPh) * 0.75f * bAmpE * bAcentoG;
      }
      bajo *= (1.0f - 0.70f * duck);                                   // el kick hace pumping sobre el bajo
    }
    bajo *= vB;
    MARCA(1)

    // ── CAPA 2 · KICK + CLAP ──
    float kick = 0, clap = 0;
    if (kAmp > 0.0004f) {
      kPh += incF(kFin + (kF0 - kFin) * kPit);
      float hp = nz - kPrev; kPrev = nz;
      kick = (sat3(sinPh(kPh) * kDrive * 0.75f) * 1.25f * kAmp + (kClk > 0.01f ? hp * kClk * 0.16f * kAmp : 0.0f));   // una sola parcial fuerte: se puede saturar
      kPit *= 0.9988f; kAmp *= kDecC; kClk *= 0.9940f;
    } else kAmp = 0;
    duck *= 0.99982f;                                                  // pumping: se suelta en ~120 ms
    if (clBursts > 0 || clAmp > 0.0004f) {
      if (clBursts > 0 && --clTimer <= 0) { clAmp = 1.0f; clBursts--; clTimer = 380.0f; if (clBursts == 0) clDec = 0.99985f; }
      clap = svfBP(clF, clK, nz) * clAmp * 0.28f; clAmp *= clDec;
    } else clAmp = 0;
    kick *= vR; clap *= vR;
    MARCA(2)

    // ── CAPA 2 · HATS + CONGA + SHAKER ──
    float hat = 0, conga = 0, shk = 0;
    if (hAmp > 0.0004f) { hat = svfBP(hF, hK, nz) * hAmp * 0.22f; hAmp *= hDec; } else hAmp = 0;
    if (pgAmp > 0.0004f) { pgPh += incF(pgF * (0.80f + 0.20f * pgPit)); conga = sinPh(pgPh) * pgAmp * 0.30f; pgPit *= 0.9990f; pgAmp *= pgDec; } else pgAmp = 0;
    if (shAmp > 0.0004f) { shk = svfBP(shF, shK, nz) * shAmp * 0.16f; shAmp *= shDec; } else shAmp = 0;
    hat *= vR; conga *= vR; shk *= vR;
    MARCA(3)

    // ── CAPA 3 · PAD ──
    float pad = 0;
    pdAmp += ((padOn ? 1.0f : 0.0f) - pdAmp) * (padOn ? 0.00008f : 0.00020f);
    if (pdAmp > 0.0004f) {
      rampaAvanza(pdR);
      float sum = 0;
      for (int k = 0; k < 3; k++) { pdPh[k] += pdInc[k]; sum += leeTabla(tabSaw[pdMip[k]], pdPh[k]); }
      pad = svfLP(pdF, pdR.cur, sum * 0.12f) * pdAmp * (1.0f - 0.50f * duck) * vS;
    }
    MARCA(4)

    // ── CAPA 3 · ARPEGIO ──
    float arp = 0;
    if (apAmp > 0.0004f) {
      rampaAvanza(apR);
      apPh += apInc;
      arp = svfLP(apF, apR.cur, leeTabla(tabSaw[apMip], apPh)) * apAmp * 0.34f * arpK * (1.0f - 0.30f * duck) * vS;
      apAmp *= apDec;
    } else apAmp = 0;
    MARCA(5)

    // ── BUS SECO y ECO ping-pong (sincronizado: corchea con puntillo; dos cabezas con crossfade) ──
    float dryL = bajo + kick + clap + hat * hGL + conga + shk * shGL + pad + arp;
    float dryR = bajo + kick + clap + hat * hGR + conga + shk * shGR + pad + arp;
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
    ecoFb += ((0.36f + 0.34f * vEsp) - ecoFb) * 0.0006f;
    // lo que entra a los efectos: arpegio, pad y textura según el ESPACIO (capa 3); la batería sólo un poco (y escalada por SU volumen)
    float sEco = arp * (0.25f + 0.85f * vEsp) + pad * 0.12f * vEsp + clap * 0.30f + hat * 0.25f;
    float wL = sEco + lzR * ecoFb, wR = sEco * 0.9f + lzL * ecoFb;
    if (wL > 0.8f || wL < -0.8f) wL = limitador(wL);
    if (wR > 0.8f || wR < -0.8f) wR = limitador(wR);
    ecoL[ecoW] = (int16_t)(wL * 30000.0f); ecoR[ecoW] = (int16_t)(wR * 30000.0f);
    if (++ecoW >= ECO_N) ecoW = 0;
    float retEL = lzL * (0.40f + 0.60f * vEsp), retER = lzR * (0.40f + 0.60f * vEsp);
    MARCA(6)

    // ── CAPA 3 · TEXTURA: riser e IMPACTO ──
    float ris = 0, imp = 0;
    if (texOn || risAmp > 0.001f) {
      float objetivo = texOn ? risProg * risProg : 0.0f;
      risAmp += (objetivo - risAmp) * (texOn ? 0.0020f : 0.0030f);
      if (risAmp > 0.001f) { risPh += risInc; ris = (svfBP(risF, risK, nz) * 0.9f + sinPh(risPh) * 0.35f) * risAmp * 0.30f * vT; }
    }
    if (impAmp > 0.0004f) {
      impPh += incF(30.0f + 60.0f * impPit);
      imp = (sinPh(impPh) * impAmp * 0.85f + svfLP(impF, impK, nz) * impNz * 0.55f) * vT;
      impPit *= 0.99994f; impAmp *= 0.99996f; impNz *= 0.99985f;
    } else impAmp = 0;
    MARCA(7)

    // ── PLACA (Dattorro) a 22.05 kHz ──
    xAcc += (arp * 0.7f + ris * 0.6f + imp * 0.5f + pad * 0.5f * vEsp + (retEL + retER) * 0.30f + (clap + hat) * 0.20f);
    if (pFase == 0) { pFase = 1; }
    else {
      pFase = 0;
      float envio = 0.02f + 0.55f * vEsp;
      float x = xAcc * 0.5f * envio * 3.0f; xAcc = 0;
      bwIn += 0.80f * (x - bwIn);
      float decay = 0.50f + 0.14f * vEsp;
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
    MARCA(8)

    // ── MEZCLA → DC → compresor → limitador → dither ──
    float colaG = (0.20f + 0.65f * vEsp) * (1.0f - 0.35f * duck);   // la cola también "respira" con el bombo
    float mL = dryL + ris + imp + retEL + pL * colaG;
    float mR = dryR + ris + imp + retER + pR * colaG;
    float yL = mL - dcXL + DC_R * dcYL; dcXL = mL; dcYL = yL;
    float yR = mR - dcXR + DC_R * dcYR; dcXR = mR; dcYR = yR;
    float pk = fabsf(yL) > fabsf(yR) ? fabsf(yL) : fabsf(yR);
    cmpEnv += (pk - cmpEnv) * (pk > cmpEnv ? 0.0060f : 0.00020f);
    if ((i & 7) == 0) {                                                // el cálculo de la ganancia (lleva una división) sólo cada 8 muestras
      cmpGT = 1.0f;
      if (cmpEnv > 0.50f) cmpGT = (0.50f + (cmpEnv - 0.50f) * (1.0f / 2.2f)) / cmpEnv;
    }
    cmpGan += (cmpGT - cmpGan) * (cmpGT < cmpGan ? 0.0110f : 0.00016f);
    const float gain = cmpGan * 0.90f;
#if DIAG
    { float a = fabsf(yL * gain); if (a > dgPk) dgPk = a; dgN++; if (a > 0.75f) dgSobre++; if (cmpGan < 0.9f) dgComp++; }
#endif
    float oL2 = limitador(yL * gain), oR2 = limitador(yR * gain);
    lpOL += 0.78f * (oL2 - lpOL); lpOR += 0.78f * (oR2 - lpOR); oL2 = lpOL; oR2 = lpOR;   // techo de agudos (~10 kHz) DESPUÉS del limitador
    uint32_t r1 = xr(), r2 = xr();                                      // dither triangular ±1 LSB: dos uniformes de 16 bits por canal con un solo número aleatorio
    float dL = (float)((int)(r1 & 0xFFFF) - (int)(r1 >> 16)) * (1.0f / 65536.0f);
    float dR = (float)((int)(r2 & 0xFFFF) - (int)(r2 >> 16)) * (1.0f / 65536.0f);
    buffer[i * 2]     = (int16_t)((int)(oL2 * 32000.0f + dL + 32768.5f) - 32768);
    buffer[i * 2 + 1] = (int16_t)((int)(oR2 * 32000.0f + dR + 32768.5f) - 32768);
    MARCA(9)

    acumBajo += bAmpE; acumKick += kAmp; acumHat += hAmp; acumArp += apAmp; acumPad += pdAmp;
  }
#if DIAG
  ccN++;
#endif
  g_bajo = acumBajo * inv; g_kick = acumKick * inv > 1 ? 1 : acumKick * inv; g_hat = acumHat * inv * 2.0f > 1 ? 1 : acumHat * inv * 2.0f;
  g_arp = acumArp * inv; g_pad = acumPad * inv; g_tex = risAmp > 1.0f ? 1.0f : risAmp;
  { uint32_t bp = tickB % TPB; g_pul[0] = (bp < 5) ? 1.0f - (float)bp * 0.18f : 0.0f;
    bp = tickR % TPB; g_pul[1] = (bp < 5) ? 1.0f - (float)bp * 0.18f : 0.0f;
    bp = tickS % TPB; g_pul[2] = (bp < 5) ? 1.0f - (float)bp * 0.18f : 0.0f; }
  g_estado[0] = runB ? 1 : (pendB ? 2 : 0); g_estado[1] = runR ? 1 : (pendR ? 2 : 0); g_estado[2] = runS ? 1 : (pendS ? 2 : 0);
  g_zona[0] = zB; g_zona[1] = zR; g_zona[2] = zS;
  g_bPat = bPat; g_rPat = rPat; g_prog = progBanco; g_tipo = bTipo; g_oct = bOct; g_dir = arpDir;
  g_arpOn = (arpDir != 4);
  g_slide = bSlideOn; g_perc = rPerc; g_kickOn = rKickOn; g_padOn = padOn; g_fill = fillOn;
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
  for (int i = 0; i <= 1024; i++) {
    float th = 2.0f * (float)M_PI * (float)(i & 1023) / 1024.0f;
    SIN[i]  = sinf(th);
    SUBW[i] = (sinf(th) + 0.14f * sinf(2.0f * th)) * 0.92f;
  }
  construirTablas();
  resetAudio();
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812, ONBOARD_PIN, GRB>(onboard, 1);
  FastLED.setBrightness(LED_BRIGHT); FastLED.clear(); FastLED.show();
  fijarTempo(0, ZONA_INI); fijarTempo(1, ZONA_INI); fijarTempo(2, ZONA_INI); ecoDA = ecoDB = (int)(18.0f * tlS); ecoX = 0;
  acordeAct = PROG_A[0][0]; acordeIdx = 0; aplicarAcorde();
  for (int k = 0; k < 3; k++) pdInc[k] = pdIncT[k];
  bR1.cur = coefDe(gDe(300.0f), 0.3f); bR2.cur = coefDe(gDe(300.0f), 1.4142f); bR1.paso = bR2.paso = Coef{0, 0, 0, 0};
  pdR.cur = coefDe(gDe(1500.0f), 1.25f); pdR.paso = Coef{0, 0, 0, 0};
  apR.cur = coefDe(gDe(2400.0f), 0.77f); apR.paso = Coef{0, 0, 0, 0};
  i2s_init();
  xTaskCreatePinnedToCore(audioTask,   "audio",   8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 6144, NULL,  3, NULL, 0);
  capaT = millis();
  xTaskCreatePinnedToCore(oledTask,    "oled",    4096, NULL,  1, NULL, 0);
}
void audioTask(void *)   { for (;;) renderBuffer(); }
void controlTask(void *) { for (;;) { pasoControl(); vTaskDelay(1); } }
void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
