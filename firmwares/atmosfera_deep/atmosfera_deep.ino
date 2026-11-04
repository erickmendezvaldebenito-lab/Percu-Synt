// ==============================================================================================================================================
// PERCU-SYNTH — ATMÓSFERA DEEP (ambiente que muta · escala que crece · todo en el mismo reloj) — GC Lab Chile
// ==============================================================================================================================================
// Desarrollado por: Gonzalo - GC Lab Chile
// Licencia de Software: MIT License (https://opensource.org/licenses/MIT)
// Licencia de Hardware: CERN Open Hardware Licence v2 - Permissive (CERN-OHL-P)
//
// Puedes usar, modificar y distribuir este código y hardware, siempre que se mantenga
// la atribución a GC Lab Chile. Se entrega "tal cual", sin garantías de ningún tipo.
// ==============================================================================================================================================
// REPOSITORIO: https://github.com/GC-Lab-Gonzalo/Percu-Synth
// ==============================================================================================================================================
// HARDWARE (usado por este firmware)
// ==============================================================================================================================================
// - Microcontrolador ESP32-S3
// - DAC PCM5102 vía I2S — estéreo 44.1 kHz · 16-bit |LCK -> 39, DIN -> 40, BCK -> 41|
// - 6 LEDs WS2812 SMD internos de la placa |DATA -> 46| (indicadores: una capa por LED)
// - 5 Botones con pull-up |BTN1 -> 44, BTN2 -> 42, BTN3 -> 0, BTN4 -> 45, BTN5 -> 47|
// - 4 Potenciómetros analógicos |POT1 -> ADC1, POT2 -> ADC2, POT3 -> ADC8, POT4 -> ADC10|
// - No usa IMU, microSD, pantalla ni MIDI. Sin Serial (todo el CPU es del audio).
// ==============================================================================================================================================
// ARDUINO IDE — settings críticos
// ==============================================================================================================================================
// - Board              : ESP32S3 Dev Module
// - Flash Mode         : DIO          (¡OPI rompe I2S!)
// - Partition Scheme   : Default 4MB with spiffs (o cualquiera: el firmware ocupa ~300 KB)
// - PSRAM              : no hace falta
// ==============================================================================================================================================
// LIBRERÍAS REQUERIDAS
// ==============================================================================================================================================
// - ESP32 Arduino core ≥ 3.x (incluye driver/i2s_std.h y Preferences)
// - FastLED (gestor de librerías de Arduino)
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// Un ambiente profundo de la escuela deep / dub techno / ambient electrónico (la estética de sellos y
// festivales underground como Dekmantel: graves largos, acordes que respiran en el eco, pulsos suaves que
// aparecen de a poco). Es una MÁQUINA DE ATMÓSFERA: no se toca nota por nota, se la deja crecer.
//
// TODO VA EN UN SOLO RELOJ (la semicorchea). Los acordes cambian en el borde del compás, las notas caen en
// la rejilla, el eco está en pasos y las pausas cortan en pasos: nada puede salirse de tiempo. Y TODO ESTÁ EN
// LA ESCALA: cada nota se calcula como un GRADO del modo, así que ninguna puede salir de tono.
//
// LA ESCALA CRECE CON EL POT3. Cada Play sortea una tonalidad y un modo de los tres más usados en el deep y
// el ambient (EÓLICO = menor natural · DÓRICO · FRIGIO). Con el POT3 abajo sólo existen la tónica y la quinta
// (un drone pelado); al subirlo se agregan, de a una, la tercera, la séptima, la cuarta, la segunda y la sexta
// (esas dos últimas son las que le dan su color a cada modo). Cada vez que entra una nota nueva entra también
// una CAPA nueva, para que la atmósfera crezca junto con la escala:
//     DRON → PAD → TEXTURA → PULSO (acordes de dub) → ARPEGIO → HATS → BOMBO
//
// EL POT2 ES LA MUTACIÓN del ambiente: abajo es un solo acorde quieto; hacia arriba el acorde empieza a
// moverse (cada 8, 4, 2 y 1 compases, siempre por grados de la escala), el pad abre y cierra su brillo, el
// dron respira con un batido lento, la textura cambia de nota y el arpegio se reescribe. Nada salta: cada
// cambio se arma en el borde del compás y llega deslizado.
//
// GUARDADO: la escena (tonalidad, modo, tempo, semilla de los patrones) y los modos de filtro / delay / pausas
// / sinte se guardan en la memoria flash del chip (Preferences) y vuelven al encender. El primer Play tras
// encender toca la MISMA escena que dejaste; los Play siguientes sortean una nueva.
//
// ARQUITECTURA: audio en su propia tarea en el CORE 1; botones, pots y LEDs en el CORE 0 a 1 kHz. Los
// controles sólo dejan PEDIDOS que el audio aplica en el borde de un bloque.
// ==============================================================================================================================================
// FUNCIONAMIENTO — un botón = una función, en el flanco de presión. Los pots significan siempre lo mismo.
// ==============================================================================================================================================
//   BTN1  PLAY / STOP. El Play sortea una escena nueva (la primera vez tras encender repite la guardada).
//   BTN2  FILTRO: apagado → pasa-bajos → pasa-altos → pasa-banda. El corte lo da el POT4.
//   BTN3  SINTE: agrega / quita una voz melódica cuyo timbre sigue al POT2: abajo SONAR (ping profundo),
//         al medio CAMPANA (FM de cristal), arriba CORO (voz de formantes). Toca notas de la escala.
//   BTN4  DELAY: apagado → corchea con puntillo → negra → corchea (ping-pong, en el tempo).
//   BTN5  PAUSAS sincronizadas: nada → RESPIRA (calla el 4.º tiempo de cada compás) → CORTE (calla el
//         último compás de cada frase de 4) → DUB (tiempos sorteados con la semilla). Las pausas callan
//         pad, pulso, arpegio, hats, bombo y sinte; el dron y la textura siguen, y el eco y la reverb
//         siguen sonando en el hueco.
//
//   POT1  VOLUMEN
//   POT2  MUTACIÓN del ambiente (0 = quieto · 1 = se mueve cada compás) y timbre del sinte
//   POT3  ESCALA y ATMÓSFERA: notas y capas que existen (ver arriba)
//   POT4  CORTE del filtro (sólo suena si el filtro está encendido con BTN2). Este pot no estaba en el
//         diseño original: es el único control que le faltaba al filtro.
//
// LEDs de la placa (el color es el del modo: violeta eólico · cian dórico · rojo frigio):
//   LED0 dron · LED1 pad · LED2 textura/pulso · LED3 arpegio/sinte · LED4 hats/bombo · LED5 pulso del compás
//   (se apagan a un cuarto cuando una pausa calla la música; LED5 magenta = el audio usa más del 80 % del CPU)
//
// MODO DE USO:
// 1. Sube el firmware, aprieta BTN1 y sube el POT1. Con el POT3 abajo oyes sólo el dron.
// 2. Sube despacio el POT3: la escena se llena. Al llegar arriba suena el bombo.
// 3. Mueve el POT2 para que el ambiente cambie; BTN3 agrega el sinte; BTN4 el eco; BTN2 + POT4 el filtro.
// 4. BTN5 para meter silencios en el tiempo. BTN1 dos veces = otra escena.
// ==============================================================================================================================================

// DIAG 1 = cada 2 s imprime por Serial0 (el conector UART de la placa) la carga real del audio. Uso normal: 0.
#ifndef DIAG
#define DIAG 0
#endif
#pragma GCC optimize ("O2")
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <FastLED.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

// ─── Tipos (arriba del todo para que el IDE de Arduino genere bien los prototipos) ───
struct Svf  { float c1, c2; };                                  // SVF de Zavalishin (TPT): estado
struct SvfC { float a1, a2, a3, k; };                           // y coeficientes
struct Env  { float v; bool atk, rapido; };                     // envolvente: ataque → caída → cola rápida → libre
struct Guardado {                                               // lo que se guarda en la flash (16 bytes)
  uint32_t semilla; uint16_t suma;
  uint8_t magia, version, tonica, modo, bpm, filtro, delay, pausa, sinte, relleno;
};

// ─── Pines ───
#define I2S_LCK   39
#define I2S_DIN   40
#define I2S_BCK   41
#define SAMPLE_RATE     44100
#define BUFFER_SAMPLES  128
#define LED_PIN        46
#define NUM_LEDS        6
#define LED_BRIGHT    120
const uint8_t BTN_PIN[5] = { 44, 42, 0, 45, 47 };
const uint8_t POT_PIN[4] = { 1, 2, 8, 10 };
const unsigned long DEBOUNCE_MS = 30;

const float SR     = (float)SAMPLE_RATE;
const float INV_SR = 1.0f / (float)SAMPLE_RATE;
const float PI2    = 6.28318530718f;

// ==============================================================================================================================================
// SONIDO — lo fijo se ajusta acá
// ==============================================================================================================================================
const float MASTER = 0.80f;               // ganancia general con POT1 al máximo
const float BPM_MIN = 108.0f, BPM_MAX = 124.0f;   // el tempo se sortea en cada escena (deep / dub techno)

// Capas: 0 dron · 1 pad · 2 textura · 3 pulso · 4 arpegio · 5 hats · 6 bombo · 7 sinte
enum { C_DRON, C_PAD, C_TEXT, C_PULSO, C_ARP, C_HATS, C_BOMBO, C_SINTE, N_CAPAS };
// El POT3 enciende cada capa con una rampa suave entre T0 y T1. (dron y sinte no dependen del POT3)
const float CAP_T0[N_CAPAS]  = { -1.0f, 0.06f, 0.20f, 0.34f, 0.50f, 0.64f, 0.80f, -1.0f };
const float CAP_T1[N_CAPAS]  = {  0.0f, 0.16f, 0.30f, 0.44f, 0.60f, 0.74f, 0.90f,  0.0f };
#ifdef SIMULADOR
#define CSIM                                  // el simulador de PC necesita silenciar capas para medirlas de a una
#else
#define CSIM const
#endif
CSIM float NIV_CAP[N_CAPAS] = {  0.23f, 0.80f, 0.52f, 0.80f, 0.80f, 0.55f, 0.42f, 0.70f };   // nivel de cada capa (el dron, el más bajo: antes era un sub al máximo)
const float SEND_D[N_CAPAS]  = {  0.00f, 0.06f, 0.00f, 0.45f, 0.30f, 0.10f, 0.00f, 0.30f };   // envío al delay
const float SEND_R[N_CAPAS]  = {  0.02f, 0.20f, 0.18f, 0.20f, 0.20f, 0.08f, 0.00f, 0.30f };   // envío a la reverb
const float SC_CAP[N_CAPAS]  = {  0.45f, 0.35f, 0.25f, 0.30f, 0.15f, 0.00f, 0.00f, 0.10f };   // cuánto agacha el bombo
const bool  CAP_GATE[N_CAPAS]= { false, true, false, true, true, true, true, true };           // las pausas la callan

// ==============================================================================================================================================
// ARMONÍA — todo se escribe en GRADOS del modo: ninguna nota puede salir de tono.
// ==============================================================================================================================================
#define NUM_MODOS 3
const int8_t MODO[NUM_MODOS][7] = {
  { 0, 2, 3, 5, 7, 8, 10 },   // 0 EÓLICO  (menor natural)
  { 0, 2, 3, 5, 7, 9, 10 },   // 1 DÓRICO  (menor con la sexta mayor)
  { 0, 1, 3, 5, 7, 8, 10 }    // 2 FRIGIO  (segunda menor)
};
// En qué ORDEN van entrando los grados al subir el POT3: tónica, quinta, tercera, séptima, cuarta, segunda, sexta.
// (la segunda y la sexta son las que definen el color de cada modo: llegan al final)
const uint8_t ORDEN[7] = { 0, 4, 2, 6, 3, 1, 5 };
// Raíces de acorde cuya tríada es CONSONANTE en ese modo (en el frigio, por ejemplo, el grado 5 es disminuido
// y no está). En orden de preferencia: i, VI, III, VII, iv, v ...
const uint8_t ACORDES[NUM_MODOS][6] = {
  { 0, 5, 2, 6, 3, 4 },       // eólico: i VI III VII iv v
  { 0, 3, 6, 2, 1, 4 },       // dórico: i IV VII III ii v
  { 0, 1, 5, 2, 6, 3 }        // frigio: i bII bVI bIII bvii iv
};
const uint8_t TONICAS[6] = { 2, 4, 5, 7, 9, 0 };   // Re, Mi, Fa, Sol, La, Do: las que más se oyen en el deep

// ==============================================================================================================================================
// ESTADO — del audio (core 1)
// ==============================================================================================================================================
// Lo publica el audio para que el core 0 lo lea (LEDs y guardado)
volatile uint8_t  pubJugando = 0, pubTonica = 2, pubModo = 0, pubBpm = 118, pubFiltro = 0, pubDelay = 0, pubPausa = 0, pubSinte = 0;
volatile uint32_t pubSemilla = 1;
volatile uint16_t pubEscenaSeq = 0;
volatile float    pubGan[N_CAPAS];
volatile float    pubFlash[N_CAPAS];            // destellos de los LEDs: el audio pone 1.0, el control los apaga
volatile float    pubGate = 1.0f, pubCarga = 0.0f, pubCargaMax = 0.0f; volatile uint8_t pubAligera = 0;
// Los deja el control para el audio
volatile uint8_t  reqPlay = 0, reqFiltro = 0, reqSinte = 0, reqDelay = 0, reqPausa = 0;
volatile float    pVol = 0.5f, pMuta = 0.0f, pAtm = 0.0f, pCorte = 0.5f;
volatile uint8_t  pEtapa = 0;                   // zona del POT3 (0..5): notas disponibles = etapa + 2

// ─── Escena ───
bool     jugando = false;
bool     escenaCargada = false;                 // había una escena válida en la flash
bool     primerPlay = true;                     // el primer Play tras encender repite la guardada
uint8_t  tonica = 2, modo = 0, bpm = 118;
uint32_t semilla = 1;
uint8_t  filtroM = 0, delayM = 0, pausaM = 0;
bool     sinteOn = false, sinteYa = false;   // sinteYa: al encenderlo, suena una nota en el paso siguiente (no esperar al patrón)
uint8_t  visPlay = 0, visFiltro = 0, visSinte = 0, visDelay = 0, visPausa = 0;

// ─── Azar ───
uint32_t rngA = 0x9E3779B9u;                    // ruido de audio
uint32_t rngS = 1;                              // azar de la escena: sembrado con la semilla (misma semilla = mismos patrones)
inline float ruido() { rngA ^= rngA << 13; rngA ^= rngA >> 17; rngA ^= rngA << 5; return (float)(int32_t)rngA * (1.0f / 2147483648.0f); }
inline uint32_t sorteoS() { rngS ^= rngS << 13; rngS ^= rngS >> 17; rngS ^= rngS << 5; return rngS; }
inline float azarS() { return (float)(sorteoS() >> 8) * (1.0f / 16777216.0f); }
// El instante EXACTO de la pulsación es la mejor fuente de entropía de una placa sin reloj.
inline uint32_t entropia() { uint32_t t = (uint32_t)micros() * 2654435761u; t ^= (uint32_t)millis() << 7; return t ? t : 1u; }

// ─── Reloj (semicorcheas) ───
float    sps = 6000.0f, faltan = 0.0f;
int      paso = 0, compas = 0, compasFrase = 0;
int      etapa = 0, nNotas = 2;                 // notas de la escala disponibles
uint8_t  rango[7];                              // posición de cada grado en ORDEN (0 = la primera en entrar)
float    gate = 1.0f, gateT = 1.0f;             // pausas
float    run = 0.0f;                            // fundido de Play / Stop
float    volC = 0.0f;
float    gC[N_CAPAS], gT[N_CAPAS];             // ganancia actual y objetivo de cada capa
float    duck = 0.0f; int duckHold = 0;          // sidechain del bombo

// ─── Patrones de la escena (salen de la semilla) ───
bool     stabPat[32], arpAct[32], sintePat[32];
uint8_t  arpDeg[32], arpOct[32], sinteDeg[32];
uint8_t  hatPat[16];                            // 0 nada · 1 fantasma · 2 acento
uint16_t pausaMask[4];                          // por compás de la frase: bit = tiempo callado (modo DUB)

// ─── Armonía en curso ───
int      acorde = 0;                            // grado raíz del acorde
int      cuentaAcorde = 0;
float    brillo = 700.0f;                       // corte objetivo del pad (Hz)
float    padCents = 5.0f;

// ─── Utilidades de sonido ───
float sinTab[2049], subTab[2049];                  // subTab = seno + 0.30 × su 2.º armónico (el dron pesa en los parlantes chicos con UNA lectura)
inline float tabT(const float *t, float ph) {    // tabla con interpolación, ph en [0,1)
  float x = ph * 2048.0f; int i = (int)x; float f = x - (float)i;
  return t[i] + (t[i + 1] - t[i]) * f;
}
inline float sinT(float ph) { return tabT(sinTab, ph); }
inline void panG(float p, float &gl, float &gr) {   // potencia constante: gr = sin θ, gl = cos θ, θ = (1+p)·π/4
  const float x = (1.0f + p) * 0.125f; gr = sinT(x); gl = sinT(0.25f - x);
}
inline float polyBlep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}
inline float softSat(float x) {
  if (x >  2.5f) x =  2.5f;
  if (x < -2.5f) x = -2.5f;
  return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
inline float kTau(float tau) { return 1.0f - expf(-1.0f / (tau * SR)); }     // coef. de un suavizado de constante tau
inline float decTau(float tau) { return expf(-1.0f / (tau * SR)); }            // multiplicador por muestra de una caída exponencial
inline float hz(float midi) { return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f); }
inline float softCub(float x) {                 // saturación cúbica suave, SIN división: x − x³/3, tope en ±2/3
  if (x >  1.0f) return  0.66667f;
  if (x < -1.0f) return -0.66667f;
  return x - x * x * x * (1.0f / 3.0f);
}
// Limitador suave y sin estado: sólo actúa por encima de 0.75 (en uso normal los picos quedan en 0.5–0.7). Un limitador con lookahead
// costaba ~5 % del CPU y en todas las pruebas nunca llegó a actuar.
inline float limSuave(float x) {
  const float a = fabsf(x);
  if (a < 0.75f) return x;
  float t = (a - 0.75f) * 4.0f; if (t > 3.0f) t = 3.0f;
  const float o = 0.75f + 0.25f * softSat(t);
  return x < 0.0f ? -o : o;
}

SvfC svfCoef(float fc, float q) {
  if (fc < 20.0f) fc = 20.0f;
  if (fc > 0.45f * SR) fc = 0.45f * SR;
  float g = tanf(3.14159265f * fc * INV_SR), k = 1.0f / q;
  float a1 = 1.0f / (1.0f + g * (g + k));
  SvfC c; c.a1 = a1; c.a2 = g * a1; c.a3 = g * c.a2; c.k = k;
  return c;
}
inline void svf(Svf &s, const SvfC &c, float x, float &lp, float &bp, float &hp) {
  float v3 = x - s.c2;
  float v1 = c.a1 * s.c1 + c.a2 * v3;
  float v2 = s.c2 + c.a2 * s.c1 + c.a3 * v3;
  s.c1 = 2.0f * v1 - s.c1;
  s.c2 = 2.0f * v2 - s.c2;
  lp = v2; bp = v1; hp = x - c.k * v1 - v2;
}

// ─── Envolventes: NUNCA se libera una voz cortándola (un escalón es un click de banda ancha). Al cruzar
// −48 dB pasa a una cola de 1.5 ms y se libera a −82 dB.
const float ENV_UMBRAL = 0.004f, ENV_FIN = 0.00008f;
float kEnvRap;                                   // caída de la cola rápida (tau 1.5 ms)
inline float envPaso(Env &e, float kAt, float dec) {
  if (e.atk) { e.v += (1.0f - e.v) * kAt; if (e.v > 0.97f) e.atk = false; }
  else if (e.rapido) { e.v *= kEnvRap; if (e.v < ENV_FIN) e.v = 0.0f; }
  else { e.v *= dec; if (e.v < ENV_UMBRAL) e.rapido = true; }
  return e.v;
}
inline void envDisparo(Env &e) { e.atk = true; e.rapido = false; }   // parte de donde esté: sin saltos
inline bool envViva(const Env &e) { return e.v > 0.0f || e.atk; }

// Constantes de tiempo (se llenan en prepararSintesis)
float kPadDip, kPadRet, kGan, kRunUp, kRunDn, kVol, kPadGlideB, kPadGainB, kPadBB, kGateAb, kGateCe, kDuckAt, kDuckRl, kPan, kW;
float kPanB, kSnGl, kAtPluck, kAtStab, kAtSonar, kAtBell, kAtCoro;
float decPluck, decPluckF, decStab, decStabF, decHat, decHatAb, decSonar, decSonar2, decBell, decBellI, decCoro;
float kKickA, kKickA2, kKickM, kKickP;

// ==============================================================================================================================================
// CAPAS
// ==============================================================================================================================================
// ─── DRON: sub en la tónica (+ 2.º armónico para los parlantes chicos), quinta y octava ───
float dronF0 = 41.2f, dronPh[3] = { 0, 0.13f, 0.57f }, dronMuta = 0.0f;
// ─── PAD: acorde de 4 notas × 2 sierras desafinadas, deslizando entre acordes ───
float padFT[4] = { 130, 196, 155, 233 }, padFC[4] = { 130, 196, 155, 233 }, padG[4], padGT[4];
float padPh[4][2], padGL[4][2], padGR[4][2];
Svf   padSvfL, padSvfR; SvfC padC;
float padCut = 700.0f, padHpL = 0, padHpR = 0;
float padDtA[4], padDtB[4], padGe[4], padGeD[4], padFad[4] = { 1, 1, 1, 1 }, padBc = 1.0f, padBd = 0.0f; uint8_t padSt[4]; bool padDoble = true;   // padFad/padSt: al cambiar de acorde la voz baja, salta y vuelve (sin glissando)
float stabHp = 0, arpHp = 0, snHp = 0;               // pasa-altos de carril de cada capa melódica   // padDoble: 2 sierras por nota (el regulador de carga lo apaga)
// ─── TEXTURA: ruido por dos resonadores afinados a notas de la escala ───
float texFT[2] = { 700, 1100 }, texFC[2] = { 700, 1100 };
Svf   texSvf[2]; SvfC texC[2];
// ─── PULSO: acorde de dub (3 sierras) por un pasa-bajos con envolvente ───
Env   stabE; float stabFE = 0, stabF[3], stabPh[3]; Svf stabSvf; SvfC stabC;
// ─── ARPEGIO ───
Env   arpE; float arpFE = 0, arpF = 220, arpPh[2]; Svf arpSvf; SvfC arpC; float arpPan = 0, arpPanT = 0, arpGL = 0.7071f, arpGR = 0.7071f;
// ─── HATS (ruido por un pasa-banda, nunca un pasa-altos pelado) ───
Svf   hatSvf; SvfC hatC; Env hatE; float hatAmp = 0.0f, hatPan = 0.0f, hatGL = 0.7071f, hatGR = 0.7071f; bool hatAb = false;
// ─── BOMBO: afinado a la tónica, tres bandas (fundamental + 2.º armónico + mazo de RUIDO) ───
float kickPh = 0, kickF = 48.0f, kickEnv = 0, kickEnv2 = 0, kickMall = 0, kickPit = 0; bool kickPend = false; Svf kickSvf; SvfC kickC;
// ─── SINTE (BTN3): SONAR · CAMPANA FM · CORO ───
int   snTipo = 0; float snF = 220, snFT = 220, snPh1 = 0, snPh2 = 0, snPhM = 0, snPhS = 0, snPan = 0, snPanT = 0, snGL = 0.7071f, snGR = 0.7071f;
Env   snE, snE2, snEI; uint32_t snHold = 0; Svf snF1, snF2; SvfC snC1, snC2;

// ─── ESPACIO: delay ping-pong + reverb ───
#define DELAY_MAX 25000                          // 4 pasos al tempo más lento (108 BPM) = 24 500 muestras
int16_t dlyBufL[DELAY_MAX], dlyBufR[DELAY_MAX];
int     dlyW = 0, dlyTA = 6000, dlyTB = 6000; float dlyXf = 0.0f; bool dlyCruce = false;
float   dlyLpL = 0, dlyLpR = 0, dlyEn = 0, dlyEnT = 0, kDlyLp, kDlyXf;
const float DLY_FB = 0.50f, DLY_MIX = 0.42f, REV_MIX = 0.26f;
const int   DLY_PASOS[4] = { 0, 3, 4, 2 };        // corchea con puntillo · negra · corchea

#define RV_N 4
const int RV_LEN[RV_N] = { 1327, 1531, 1823, 2087 };
float rvBuf[RV_N][2100]; int rvIdx[RV_N]; float rvG[RV_N], rvLp[RV_N], rvIn = 0.3f, kRvDamp;
float apBuf1[142], apBuf2[107]; int apI1 = 0, apI2 = 0;

// ─── MASTER ───
Svf   mfL, mfR; SvfC mfC, mfD;                    // filtro de BTN2 (mfD = paso por muestra de la interpolación)
float wLp = 0, wHp = 0, wBp = 0, wLpT = 0, wHpT = 0, wBpT = 0;
int   aligera = 0;                                  // nivel del regulador de carga (0 = todo)
bool  filtAct = false;                              // el filtro maestro sólo se calcula si está encendido
float lpb0, lpb1, lpb2, lpa1, lpa2, lpx1L = 0, lpx2L = 0, lpy1L = 0, lpy2L = 0, lpx1R = 0, lpx2R = 0, lpy1R = 0, lpy2R = 0;
float dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;

static i2s_chan_handle_t tx_chan;
int16_t salida[BUFFER_SAMPLES * 2];

// ==============================================================================================================================================
// MÚSICA: grados → notas
// ==============================================================================================================================================
inline int baseMidi() { return 28 + ((tonica + 8) % 12); }      // la tónica plegada a 41–78 Hz (E1 … D#2)
inline int semiGrado(int deg) { return MODO[modo][deg % 7] + 12 * (deg / 7); }

// Si el grado que pide el patrón todavía no entró en la escala, suena el vecino más cercano que sí entró:
// así la melodía es SIEMPRE la misma y, al subir el POT3, las notas nuevas aparecen donde el patrón las quería.
int degDisponible(int d) {
  if (rango[d] < nNotas) return d;
  for (int k = 1; k < 7; k++) {
    int a = d - k, b = d + k;
    if (a >= 0 && rango[a] < nNotas) return a;
    if (b < 7 && rango[b] < nNotas) return b;
  }
  return 0;
}
int gradoAzar() { return ORDEN[sorteoS() % (uint32_t)nNotas]; }     // un grado cualquiera de los que ya existen

// ─── ARMONÍA CONJUNTA ───────────────────────────────────────────────────────────────────────────────────────
// Lo que ya suena (la tónica del dron y las notas del acorde) es contra lo que se mide todo lo demás: una 2.ª menor,
// 7.ª mayor o 9.ª menor entre dos capas es lo que se oye como "chocan", y ninguna capa melódica puede caer ahí.
int  acMidi[4];                                                     // notas del acorde en el registro del pad: raíz · QUINTA · tercera · séptima
bool acOk[4];                                                       // false = esa nota chocaría con la tónica del dron: no suena
int  anclas[6], nAnclas = 0;
int  arpMidi = -100, snMidi = -100;                                 // la nota que el arpegio / el sinte tienen sonando (las demás capas también se miden contra ella)
inline bool semitonoChoque(int a, int b) { int d = (a > b ? a - b : b - a) % 12; return d == 1 || d == 11; }
bool choca(int midi) {
  for (int i = 0; i < nAnclas; i++) if (semitonoChoque(midi, anclas[i])) return true;
  if (arpE.v > 0.10f && semitonoChoque(midi, arpMidi)) return true;
  if ((snE.v > 0.10f || snE2.v > 0.10f) && semitonoChoque(midi, snMidi)) return true;
  return false;
}
// La nota de la escala más cercana a `deg` (entre las que ya existen) que NO choca con lo que suena. oct0 = la tónica en el registro de la capa.
int notaLibre(int deg, int oct0, int tope) {
  const int d0 = degDisponible(deg);
  for (int k = 0; k < 7; k++) for (int sg = 0; sg < 2; sg++) {
    if (k == 0 && sg == 1) continue;
    const int d = sg ? d0 - k : d0 + k;
    if (d < 0 || d > 6 || rango[d] >= nNotas) continue;
    int m = oct0 + MODO[modo][d]; while (m > tope) m -= 12;
    if (!choca(m)) return m;
  }
  int m = oct0 + (semiGrado(acorde) % 12); while (m > tope) m -= 12;   // ninguna sirvió: la raíz del acorde
  return m;
}
void elegirTextura() {                                              // dos resonadores afinados a notas DEL ACORDE (nunca a una nota suelta de la escala)
  int cand[3], n = 0;
  for (int k = 0; k < 3; k++) if (acOk[k] && (k < 2 || nNotas >= 3)) cand[n++] = k;
  const int a = cand[sorteoS() % (uint32_t)n], b = cand[sorteoS() % (uint32_t)n];
  float ma = (float)(acMidi[a] + 12), mb = (float)(acMidi[b] + 24);
  while (hz(ma) > 1800.0f) ma -= 12.0f;
  while (hz(mb) > 1800.0f) mb -= 12.0f;
  texFT[0] = texFC[0] = hz(ma); texFT[1] = texFC[1] = hz(mb);        // los resonadores se re-afinan de golpe: excitados por ruido y con Q bajo, no hay escalón
}
void actualizarAcorde() {                                           // pad y textura → al acorde en curso
  const int pb = baseMidi() + 24, ton = baseMidi();
  const int dg[4] = { 0, 4, 2, 6 };                                 // raíz · QUINTA · tercera · séptima: el orden en que entran con el POT3
  nAnclas = 0; anclas[nAnclas++] = ton;
  for (int k = 0; k < 4; k++) {
    const int m = pb + semiGrado(acorde + dg[k]);
    acMidi[k] = m; padFT[k] = hz((float)m);
    acOk[k] = (k == 0) || (!semitonoChoque(m, ton) && !(k == 3 && semitonoChoque(m, acMidi[0])));   // una séptima MAYOR sobre la raíz (acorde maj7) también choca: se descarta
    if (acOk[k] && (k < 2 || (k == 2 && nNotas >= 3) || (k == 3 && nNotas >= 4))) anclas[nAnclas++] = m;
  }
  padGT[0] = 1.0f; padGT[1] = acOk[1] ? 1.0f : 0.0f;
  padGT[2] = (nNotas >= 3 && acOk[2]) ? 1.0f : 0.0f;                // la tercera entra con la 3.ª nota
  padGT[3] = (nNotas >= 4 && acOk[3]) ? 1.0f : 0.0f;                // la séptima con la 4.ª
  elegirTextura();
}
bool acordeValido() { return acorde == 0 || rango[acorde] < nNotas; }

void siguienteAcorde() {
  if (acorde != 0 && azarS() < 0.55f) { acorde = 0; return; }       // la mitad de las veces vuelve a la tónica
  int cand[6], n = 0;
  for (int i = 0; i < 6; i++) {
    int r = ACORDES[modo][i];
    const int c = semiGrado(r) % 12;                                 // una raíz a 2.ª menor / 7.ª mayor de la tónica del dron chocaría (el II del frigio)
    if (r != acorde && r != 0 && rango[r] < nNotas && c != 1 && c != 11) cand[n++] = r;   // sólo raíces que ya existen y con tríada consonante
  }
  if (n == 0) { acorde = 0; return; }
  acorde = cand[sorteoS() % (uint32_t)n];
}

// ==============================================================================================================================================
// ESCENA
// ==============================================================================================================================================
void construirEscena() {
  rngS = semilla ? semilla : 1u;
  for (int i = 0; i < 8; i++) sorteoS();                            // calentar el generador
  sps = SR * 60.0f / ((float)bpm * 4.0f);
  int hits = 0;
  for (int i = 0; i < 32; i++) {                                    // PULSO: contratiempos de corchea, algún 16avo
    int s = i & 15, q = s & 3;
    float w = q == 2 ? 0.55f : q == 3 ? 0.22f : q == 0 ? (i == 0 ? 0.0f : 0.05f) : 0.12f;
    stabPat[i] = azarS() < w; hits += stabPat[i];
  }
  while (hits < 3) { int i = (int)(sorteoS() % 32); if ((i & 3) == 2 && !stabPat[i]) { stabPat[i] = true; hits++; } }
  hits = 0;
  for (int i = 0; i < 32; i++) {                                    // ARPEGIO: 2 compases
    int q = (i & 15) & 3;
    arpAct[i] = azarS() < (q == 0 ? 0.55f : q == 2 ? 0.45f : 0.17f); hits += arpAct[i];
    arpDeg[i] = (uint8_t)(sorteoS() % 7); arpOct[i] = (uint8_t)(sorteoS() & 1);
  }
  while (hits < 6) { int i = (int)(sorteoS() % 32); if (!arpAct[i]) { arpAct[i] = true; hits++; } }
  for (int s = 0; s < 16; s++) {                                    // HATS: contratiempos + fantasmas
    int q = s & 3; hatPat[s] = q == 2 ? 2 : (q & 1) ? (azarS() < 0.22f ? 1 : 0) : 0;
  }
  hits = 0;
  for (int i = 0; i < 32; i++) {                                    // SINTE: muy escaso
    sintePat[i] = ((i & 1) == 0) && azarS() < 0.10f; hits += sintePat[i];
    sinteDeg[i] = (uint8_t)(sorteoS() % 7);
  }
  while (hits < 2) { int i = (int)(sorteoS() % 16) * 2; if (!sintePat[i]) { sintePat[i] = true; hits++; } }
  const float pc[4] = { 0.0f, 0.30f, 0.45f, 0.65f };                // PAUSAS "dub": el tiempo 1 de cada compás nunca se calla
  for (int c = 0; c < 4; c++) {
    uint16_t m = 0;
    for (int b = 1; b < 4; b++) if (azarS() < pc[c]) m |= (uint16_t)(1u << b);
    if (c == 3 && !m) m = (uint16_t)(1u << (1 + (int)(sorteoS() % 3)));
    pausaMask[c] = m;
  }
}

void reiniciarMusica() {
  paso = 0; compas = 0; compasFrase = 0; cuentaAcorde = 0; acorde = 0; faltan = 0.0f;
  brillo = 700.0f; padCents = 5.0f;
  dronF0 = hz((float)baseMidi());
  kickF = hz((float)baseMidi());                                    // bombo afinado a la tónica (41–78 Hz): por debajo de 40 Hz un parlante chico no lo reproduce
  nNotas = etapa + 2;
  actualizarAcorde(); elegirTextura();
  for (int k = 0; k < 4; k++) { padFC[k] = padFT[k]; padFad[k] = 1.0f; padSt[k] = 0; }
}

void aplicarDelay() {                                               // el tiempo del eco sigue al tempo (cruce entre dos cabezas)
  int t = DLY_PASOS[delayM] ? (int)(sps * (float)DLY_PASOS[delayM] + 0.5f) : dlyTB;
  if (t > DELAY_MAX - 2) t = DELAY_MAX - 2;
  if (t != dlyTB) { dlyTB = t; dlyXf = 0.0f; dlyCruce = true; }
  dlyEnT = delayM ? 1.0f : 0.0f;
}

void sortearEscena() {
  uint32_t e = entropia();
  semilla = e ^ (e >> 13) ^ 0xA5A5A5A5u; if (!semilla) semilla = 1;
  int mo = (int)((e >> 3) % (NUM_MODOS - 1)); modo = (uint8_t)(mo >= modo ? mo + 1 : mo);          // distinto del anterior
  int ia = 0; for (int i = 0; i < 6; i++) if (TONICAS[i] == tonica) ia = i;
  int t = (int)((e >> 9) % 5); tonica = TONICAS[t >= ia ? t + 1 : t];                               // distinta de la anterior
  bpm = (uint8_t)(BPM_MIN + (float)((e >> 15) % (uint32_t)(BPM_MAX - BPM_MIN + 1.0f)));
  pubEscenaSeq = pubEscenaSeq + 1;
}

void publicarEstado() {
  pubJugando = jugando; pubTonica = tonica; pubModo = modo; pubBpm = bpm; pubFiltro = filtroM; pubDelay = delayM;
  pubPausa = pausaM; pubSinte = sinteOn; pubSemilla = semilla;
}

void alternarPlay() {
  if (!jugando) {
    if (primerPlay && escenaCargada) { /* repite la escena guardada */ }
    else sortearEscena();
    primerPlay = false;
    etapa = pEtapa; nNotas = etapa + 2;
    construirEscena(); reiniciarMusica(); aplicarDelay();
    jugando = true;
  } else {
    jugando = false;
  }
  publicarEstado();
}

// ==============================================================================================================================================
// DISPAROS (todos en el borde de un paso)
// ==============================================================================================================================================
#ifdef SIMULADOR
void simNota(int capa, int midi);                                   // el simulador de PC anota cada nota para comprobar la escala
#endif
inline void flash(int c) { pubFlash[c] = 1.0f; }

void dispararBombo() {
  if (kickEnv > 0.01f) kickPend = true;                              // una cabeza: la anterior se apaga en ~3 ms y entra la nueva
  else { kickPh = 0.0f; kickEnv = 1.0f; kickEnv2 = 1.0f; kickMall = 1.0f; kickPit = 1.0f; }
  duckHold = (int)(0.003f * SR); flash(4);
}
void dispararHat(int acento) {
  hatAmp = acento == 2 ? 1.0f : 0.45f; hatAb = false; hatE.v = 1.0f; hatE.atk = false; hatE.rapido = false;
  hatPan = hatPan > 0 ? -0.35f : 0.35f; panG(hatPan, hatGL, hatGR); flash(4);
}
void dispararStab() {
  const int m0 = acMidi[0] + 12;                                     // un acorde de dub UNA octava sobre el pad (no lo dobla): raíz · quinta · tercera
  int m1 = (acOk[1] ? acMidi[1] : acMidi[0]) + 12;
  int m2 = (nNotas >= 3 && acOk[2]) ? acMidi[2] + 12 : acMidi[0] + 24;
  int mm[3] = { m0, m1, m2 };
  for (int i = 0; i < 3; i++) { while (mm[i] > 79) mm[i] -= 12; stabF[i] = hz((float)mm[i]); }
  envDisparo(stabE); stabFE = 1.0f; flash(2);
#ifdef SIMULADOR
  for (int i = 0; i < 3; i++) simNota(C_PULSO, mm[i]);
#endif
}
void dispararArp(int i) {
  const int m = notaLibre(arpDeg[i], baseMidi() + (arpOct[i] ? 48 : 36), 84);   // el arpegio vive más arriba que el pad y el pulso
  arpMidi = m; arpF = hz((float)m); arpPanT = arpPanT > 0 ? -0.4f : 0.4f;
  envDisparo(arpE); arpFE = 1.0f; flash(3);
#ifdef SIMULADOR
  simNota(C_ARP, m);
#endif
}
void dispararSinte(int i, float muta) {
  snPanT = ruido() * 0.5f;
  int m;
  if (muta < 0.34f) {                                                // SONAR: seno profundo con eco
    m = notaLibre(sinteDeg[i], baseMidi() + 36, 84);
    snTipo = 0; snHold = 0; snF = hz((float)m); envDisparo(snE); envDisparo(snE2);
  } else if (muta < 0.67f) {                                         // CAMPANA: FM de cristal (modulador 3.5, siempre > 2)
    m = notaLibre(sinteDeg[i], baseMidi() + 48, 84);
    snTipo = 1; snHold = 0; snF = hz((float)m); envDisparo(snE); snEI.v = 1.0f; snEI.atk = false; snEI.rapido = false;
  } else {                                                           // CORO: voz de formantes, nota larga y ligada
    m = notaLibre(sinteDeg[i], baseMidi() + 36, 79);
    snTipo = 2; snFT = hz((float)m); snF = snFT;                  // el coro SALTA de nota (un deslizamiento pasa por afinaciones que no son de ninguna nota)
    snHold = (uint32_t)(sps * 10.0f); snE.rapido = false;
  }
  snMidi = m;
#ifdef SIMULADOR
  simNota(C_SINTE, m);
#endif
  flash(3);
}
void soltarNotas() {                                                // al cambiar de acorde, lo que sonaba del acorde anterior se apaga (cola de 1.5 ms: sin escalón)
  stabE.atk = false; stabE.rapido = true; arpE.atk = false; arpE.rapido = true;
  snE.atk = false; snE.rapido = true; snE2.atk = false; snE2.rapido = true; snHold = 0;
}

bool pausaCerrada(int s, int cf) {
  switch (pausaM) {
    case 1: return s >= 12;                                          // RESPIRA: el cuarto tiempo
    case 2: return cf == 3;                                          // CORTE: el último compás de la frase
    case 3: return (pausaMask[cf] >> (s >> 2)) & 1u;                 // DUB: tiempos sorteados con la semilla
  }
  return false;
}

// Al borde de cada compás: aquí MUTA el ambiente. Todo llega deslizado (nada salta).
void eventoCompas(float muta) {
  if (muta >= 0.08f) {                                               // 0 = quieto
    int cada = muta < 0.30f ? 8 : muta < 0.60f ? 4 : muta < 0.85f ? 2 : 1;
    if (++cuentaAcorde >= cada) { cuentaAcorde = 0; const int antes = acorde; siguienteAcorde(); if (acorde != antes) soltarNotas(); actualizarAcorde(); }
    brillo += (azarS() - 0.5f) * 2.0f * muta * 900.0f;               // el pad abre y cierra
    if (brillo < 450.0f) brillo = 450.0f; if (brillo > 2200.0f) brillo = 2200.0f;
    if (azarS() < muta * 0.8f) elegirTextura();
    int k = (int)(muta * 5.0f + 0.5f);                               // el arpegio se reescribe
    for (int j = 0; j < k; j++) {
      int i = (int)(sorteoS() % 32); arpDeg[i] = (uint8_t)(sorteoS() % 7); arpOct[i] = (uint8_t)(sorteoS() & 1);
      if (azarS() < 0.4f) arpAct[i] = !arpAct[i];
    }
  } else { brillo += (700.0f - brillo) * 0.5f; }
  padCents = 5.0f + 10.0f * muta;
  dronMuta = muta;
  flash(1);
}

void eventoPaso(float muta) {
  if (paso == 0) eventoCompas(muta);
  if ((uint8_t)etapa != pEtapa) {                                     // el POT3 cambió de zona: entra / sale una nota de la escala
    etapa = pEtapa; nNotas = etapa + 2;
    if (!acordeValido()) { acorde = 0; soltarNotas(); }
    actualizarAcorde();
  }
  compasFrase = compas & 3;
  const bool cerrado = pausaCerrada(paso, compasFrase);
  gateT = cerrado ? 0.0f : 1.0f;
  if (!cerrado) {
    const int i = (compas & 1) * 16 + paso;
    if (gT[C_BOMBO] > 0.02f && (paso & 3) == 0) dispararBombo();
    if (gT[C_HATS]  > 0.02f && hatPat[paso]) dispararHat(hatPat[paso]);
    if (gT[C_PULSO] > 0.02f && stabPat[i]) dispararStab();
    if (gT[C_ARP]   > 0.02f && arpAct[i]) dispararArp(i);
    if (sinteOn && (sintePat[i] || sinteYa)) { dispararSinte(i, muta); sinteYa = false; }
  }
  if ((paso & 3) == 0) flash(5);
  if (++paso >= 16) { paso = 0; compas++; }
}

// ==============================================================================================================================================
// PEDIDOS del control (se aplican en el borde de un bloque)
// ==============================================================================================================================================
void aplicarPedidos() {
  while (visPlay != reqPlay)     { visPlay++;   alternarPlay(); }
  while (visFiltro != reqFiltro) { visFiltro++; filtroM = (filtroM + 1) & 3; }
  while (visSinte != reqSinte)   { visSinte++;  sinteOn = !sinteOn; if (sinteOn) sinteYa = true; }
  while (visDelay != reqDelay)   { visDelay++;  delayM = (delayM + 1) & 3; aplicarDelay(); }
  while (visPausa != reqPausa)   { visPausa++;  pausaM = (pausaM + 1) & 3; }
  publicarEstado();
}

// ==============================================================================================================================================
// RENDER
// ==============================================================================================================================================
void prepararSintesis() {
  for (int i = 0; i <= 2048; i++) { sinTab[i] = sinf(PI2 * (float)i / 2048.0f); subTab[i] = sinTab[i] + 0.30f * sinf(2.0f * PI2 * (float)i / 2048.0f); }
  kEnvRap = decTau(0.0015f);
  kGan = kTau(0.06f); kRunUp = kTau(0.5f); kRunDn = kTau(0.25f); kVol = kTau(0.03f);
  kPadDip = 1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 0.035f); kPadRet = 1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 0.45f);
  kPadGlideB = 1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 1.3f); kPadGainB = 1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 0.9f); kPadBB = 1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 0.5f);
  kGateAb = kTau(0.012f); kGateCe = kTau(0.004f);
  kDuckAt = kTau(0.001f); kDuckRl = kTau(0.14f); kPan = kTau(0.02f); kPanB = 1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 0.02f); kW = kTau(0.025f);
  kAtPluck = kTau(0.003f); kAtStab = kTau(0.002f); kAtSonar = kTau(0.008f); kAtBell = kTau(0.002f); kAtCoro = kTau(0.12f);
  decPluck = decTau(0.22f); decPluckF = decTau(0.09f); decStab = decTau(0.09f); decStabF = decTau(0.10f);
  decHat = decTau(0.028f); decHatAb = decTau(0.07f);
  decSonar = decTau(1.0f); decSonar2 = decTau(0.35f); decBell = decTau(0.70f); decBellI = decTau(0.35f); decCoro = decTau(0.70f);
  kKickA = decTau(0.16f); kKickA2 = decTau(0.10f); kKickM = decTau(0.006f); kKickP = decTau(0.035f);
  kDlyLp = 1.0f - expf(-PI2 * 2600.0f * INV_SR); kDlyXf = 1.0f / (0.05f * SR); kRvDamp = 1.0f - expf(-PI2 * 2700.0f * INV_SR);
  const float T60 = 3.2f; float gm = 0;
  for (int i = 0; i < RV_N; i++) { rvG[i] = powf(10.0f, -3.0f * (float)RV_LEN[i] / (T60 * SR)); gm += rvG[i]; }
  rvIn = 0.9f * (1.0f - gm / (float)RV_N);                            // normalizada por su realimentación: alargar la cola no la hace florecer
  for (int k = 0; k < 4; k++) {                                       // pad: pan fijo por oscilador (potencia constante)
    const float pan[4] = { -0.20f, 0.50f, -0.50f, 0.20f };
    for (int o = 0; o < 2; o++) {
      float p = pan[k] + (o ? 0.25f : -0.25f); if (p > 1) p = 1; if (p < -1) p = -1;
      panG(p, padGL[k][o], padGR[k][o]); padPh[k][o] = (float)(k * 2 + o) * 0.137f; padG[k] = 0.0f; padGT[k] = 0.0f;
    }
  }
  hatC = svfCoef(8000.0f, 1.2f); kickC = svfCoef(1400.0f, 1.2f);
  snC1 = svfCoef(450.0f, 6.0f); snC2 = svfCoef(800.0f, 8.0f);      // formantes del CORO (vocal "o": 450 / 800 Hz)
  stabC = svfCoef(800.0f, 1.3f); arpC = svfCoef(800.0f, 1.2f); padC = svfCoef(900.0f, 0.9f);
  kSnGl = kTau(0.15f);
  for (int i = 0; i < N_CAPAS; i++) { gC[i] = 0; gT[i] = 0; pubFlash[i] = 0; pubGan[i] = 0; }
  // pasa-bajos de 13 kHz (RBJ, Butterworth) — el techo de agudos, DESPUÉS del limitador
  float w0 = PI2 * 13000.0f * INV_SR, cw = cosf(w0), al = sinf(w0) / (2.0f * 0.70710678f), a0 = 1.0f + al;
  lpb0 = (1.0f - cw) * 0.5f / a0; lpb1 = (1.0f - cw) / a0; lpb2 = lpb0; lpa1 = -2.0f * cw / a0; lpa2 = (1.0f - al) / a0;
  for (int i = 0; i < 7; i++) rango[ORDEN[i]] = (uint8_t)i;
  hatE.v = 0; hatE.atk = false; hatE.rapido = true; stabE = hatE; arpE = hatE; snE = hatE; snE2 = hatE; snEI = hatE;
  mfC = svfCoef(8000.0f, 0.707f); mfD = SvfC{0, 0, 0, 0};
}

float fcFiltro(float p4, int tipo, float &q) {                        // POT4 → corte; la resonancia va atada al corte (nunca un pico arriba)
  if (tipo == 1) { q = 0.707f + 1.6f * powf(1.0f - p4, 1.5f); return 60.0f * powf(233.0f, p4); }     // pasa-bajos: 60 Hz … 14 kHz
  if (tipo == 2) { q = 0.707f + 1.2f * (1.0f - p4) * (1.0f - p4); return 25.0f * powf(160.0f, p4); } // pasa-altos: 25 Hz … 4 kHz
  q = 1.1f; return 80.0f * powf(60.0f, p4);                                                          // pasa-banda: 80 Hz … 4.8 kHz
}

void renderBuffer() {
  const uint32_t t0 = micros();
  aplicarPedidos();
  const float vol = pVol, muta = pMuta, atm = pAtm, corte = pCorte;
  const float volT = vol * vol * MASTER;

  // ── por bloque: objetivos y coeficientes ──
  for (int c = 0; c < N_CAPAS; c++) {
    float g = 1.0f;
    if (CAP_T0[c] >= 0.0f) { float x = (atm - CAP_T0[c]) / (CAP_T1[c] - CAP_T0[c]); x = x < 0 ? 0 : x > 1 ? 1 : x; g = x * x * (3.0f - 2.0f * x); }
    if (c == C_SINTE) g = sinteOn ? 1.0f : 0.0f;
    if (c == C_TEXT && aligera >= 2) g = 0.0f;
    gT[c] = g; pubGan[c] = gC[c] * run;
  }
  padCut += (brillo - padCut) * (1.0f - expf(-(float)BUFFER_SAMPLES * INV_SR / 2.0f));
  padC = svfCoef(padCut, 0.8f);
  for (int i = 0; i < 2; i++) texC[i] = svfCoef(texFC[i], 6.0f);
  { float q; float fc = fcFiltro(corte, filtroM ? filtroM : 1, q); SvfC t = svfCoef(fc, q);
    mfD.a1 = (t.a1 - mfC.a1) / (float)BUFFER_SAMPLES; mfD.a2 = (t.a2 - mfC.a2) / (float)BUFFER_SAMPLES;
    mfD.a3 = (t.a3 - mfC.a3) / (float)BUFFER_SAMPLES; mfD.k = (t.k - mfC.k) / (float)BUFFER_SAMPLES; }
  wLpT = filtroM == 1; wHpT = filtroM == 2; wBpT = filtroM == 3;
  const bool jug = jugando;
  const float dronDetR = 1.0f + 0.0006f + 0.0012f * dronMuta, dronDetL = 1.0f - 0.0006f - 0.0005f * dronMuta;   // la octava, desafinada distinto en L y R: ensancha y respira
  const float da = 1.0f + padCents * 0.000578f, db = 1.0f - padCents * 0.000578f;     // cents → razón (≈ ln2/1200)
  float agl = arpGL, agr = arpGR, sgl = snGL, sgr = snGR, ngl, ngr, adl, adr, sdl, sdr;
  arpPan += (arpPanT - arpPan) * kPanB; panG(arpPan, ngl, ngr); adl = (ngl - agl) * (1.0f / (float)BUFFER_SAMPLES); adr = (ngr - agr) * (1.0f / (float)BUFFER_SAMPLES); const float aEL = ngl, aER = ngr;
  snPan  += (snPanT  - snPan)  * kPanB; panG(snPan, ngl, ngr);  sdl = (ngl - sgl) * (1.0f / (float)BUFFER_SAMPLES); sdr = (ngr - sgr) * (1.0f / (float)BUFFER_SAMPLES); const float sEL = ngl, sER = ngr;
  // PAD: el deslizamiento entre acordes y las ganancias se actualizan POR BLOQUE (la frecuencia cambia ~0.2 % de lo que falta por bloque: inaudible)
  // y las ganancias llegan con una rampa lineal por muestra. Así no hay cuentas de suavizado dentro del bucle de las 8 sierras.
  for (int k = 0; k < 4; k++) {
    // al cambiar de nota la voz BAJA, SALTA y VUELVE a subir: un glissando de 1 s pasaba por notas que no son de ningún acorde (y chocaba)
    if (padSt[k] == 0 && fabsf(padFT[k] - padFC[k]) > 0.01f) padSt[k] = 1;
    if (padSt[k] == 1) { padFad[k] += (0.0f - padFad[k]) * kPadDip; if (padFad[k] < 0.01f) { padFC[k] = padFT[k]; padSt[k] = 2; } }
    else if (padSt[k] == 2) { padFad[k] += (1.0f - padFad[k]) * kPadRet; if (padFad[k] > 0.97f) { padFad[k] = 1.0f; padSt[k] = 0; } }
    padDtA[k] = padFC[k] * da * INV_SR; padDtB[k] = padFC[k] * db * INV_SR;
    padG[k] += (padGT[k] - padG[k]) * (padGT[k] > padG[k] ? kPadGainB : kPadDip);   // sube despacio, BAJA rápido: una nota que choca no puede seguir sonando 1 s
    padGeD[k] = (padG[k] * padFad[k] - padGe[k]) * (1.0f / (float)BUFFER_SAMPLES);       // rampa lineal de la ganancia efectiva dentro del bloque
  }
  { const float b1 = padBc + ((padDoble ? 1.0f : 0.0f) - padBc) * kPadBB; padBd = (b1 - padBc) * (1.0f / (float)BUFFER_SAMPLES); }

  for (int n = 0; n < BUFFER_SAMPLES; n++) {
    if (jug) { faltan -= 1.0f; if (faltan <= 0.0f) { faltan += sps; eventoPaso(muta); } }

    // suavizados por muestra
    run  += ((jug ? 1.0f : 0.0f) - run) * (jug ? kRunUp : kRunDn);
    volC += (volT - volC) * kVol;
    gate += (gateT - gate) * (gateT > gate ? kGateAb : kGateCe);
    for (int c = 0; c < N_CAPAS; c++) gC[c] += (gT[c] - gC[c]) * kGan;
    { float tgt = duckHold > 0 ? 1.0f : 0.0f; if (duckHold > 0) duckHold--; duck += (tgt - duck) * (tgt > duck ? kDuckAt : kDuckRl); }
    agl += adl; agr += adr; sgl += sdl; sgr += sdr;                              // paneo del arpegio y del sinte: rampa lineal dentro del bloque
    dlyEn += (dlyEnT - dlyEn) * kW;
    wLp += (wLpT - wLp) * kW; wHp += (wHpT - wHp) * kW; wBp += (wBpT - wBp) * kW;

    float dL = 0, dR = 0;                 // suma en seco
    float sdL = 0, sdR = 0, srL = 0, srR = 0;   // envíos al delay / a la reverb
#define MEZCLA(c, xl, xr) { dL += (xl); dR += (xr); sdL += (xl) * SEND_D[c]; sdR += (xr) * SEND_D[c]; srL += (xl) * SEND_R[c]; srR += (xr) * SEND_R[c]; }

    // ── DRON ── la tónica (sub + 2.º armónico) y su octava en triángulo. SIN quinta fija: chocaba con cualquier acorde que trajera una
    // sexta menor. Y el bombo se queda con el grave: cuando entra, el sub del dron cede (el "bajo pegado a full" era esto, siempre al máximo).
    { const float g = gC[C_DRON] * run * (1.0f - SC_CAP[C_DRON] * duck) * NIV_CAP[C_DRON];
      if (g > 0.0001f) {
        dronPh[0] += dronF0 * INV_SR; if (dronPh[0] >= 1.0f) dronPh[0] -= 1.0f;
        dronPh[1] += dronF0 * 2.0f * dronDetL * INV_SR; if (dronPh[1] >= 1.0f) dronPh[1] -= 1.0f;
        dronPh[2] += dronF0 * 2.0f * dronDetR * INV_SR; if (dronPh[2] >= 1.0f) dronPh[2] -= 1.0f;
        const float sub = tabT(subTab, dronPh[0]) * (1.0f - 0.75f * gC[C_BOMBO]) * 0.7071f;
        const float tl = (dronPh[1] < 0.5f ? 4.0f * dronPh[1] - 1.0f : 3.0f - 4.0f * dronPh[1]) * 0.40f;
        const float tr = (dronPh[2] < 0.5f ? 4.0f * dronPh[2] - 1.0f : 3.0f - 4.0f * dronPh[2]) * 0.40f;
        MEZCLA(C_DRON, g * (sub + tl), g * (sub + tr));
      } }

    // ── PAD ──
    { const float g = gC[C_PAD] * run * gate * (1.0f - SC_CAP[C_PAD] * duck) * NIV_CAP[C_PAD];
      if (g > 0.0001f) {
        float pl = 0, pr = 0; padBc += padBd;
        for (int k = 0; k < 4; k++) {
          padGe[k] += padGeD[k];
          if (padGe[k] < 0.0005f) continue;
          float p = padPh[k][0] + padDtA[k]; if (p >= 1.0f) p -= 1.0f; padPh[k][0] = p;
          const float sa = (2.0f * p - 1.0f) - polyBlep(p, padDtA[k]);
          float sb = 0.0f;
          if (k < 2 && padBc > 0.002f) { float q = padPh[k][1] + padDtB[k]; if (q >= 1.0f) q -= 1.0f; padPh[k][1] = q; sb = (q < 0.5f ? 4.0f * q - 1.0f : 3.0f - 4.0f * q) * padBc; }   // 2.ª voz TRIÁNGULO desafinado: más suave que otra sierra y sin PolyBLEP
          pl += padGe[k] * (sa * padGL[k][0] + sb * padGL[k][1]); pr += padGe[k] * (sa * padGR[k][0] + sb * padGR[k][1]);
        }
        float a, b, c2, l, r;
        svf(padSvfL, padC, pl * 0.16f, l, a, b); svf(padSvfR, padC, pr * 0.16f, r, c2, b);
        padHpL += 0.0196f * (l - padHpL); padHpR += 0.0196f * (r - padHpR);           // pasa-altos ~140 Hz (el acorde no pisa al sub)
        l -= padHpL; r -= padHpR;
        MEZCLA(C_PAD, l * g, r * g);
      } }

    // ── TEXTURA ──
    { const float g = gC[C_TEXT] * run * (1.0f - SC_CAP[C_TEXT] * duck) * NIV_CAP[C_TEXT];
      if (g > 0.0001f) {
        float a, b, c2, d, e2, f2; float nz = ruido();
        svf(texSvf[0], texC[0], nz, a, b, c2); svf(texSvf[1], texC[1], nz, d, e2, f2);
        MEZCLA(C_TEXT, b * texC[0].k * g * 0.9f, e2 * texC[1].k * g * 0.9f);
      } }

    // ── PULSO (acorde de dub) ──
    if (envViva(stabE)) {
      const float ev = envPaso(stabE, kAtStab, decStab); stabFE *= decStabF;
      if ((n & 31) == 0) stabC = svfCoef(300.0f + 1500.0f * stabFE, 1.2f);
      float s = 0;
      for (int k = 0; k < 3; k++) { float dt = stabF[k] * INV_SR; stabPh[k] += dt; if (stabPh[k] >= 1.0f) stabPh[k] -= 1.0f; s += (2.0f * stabPh[k] - 1.0f) - polyBlep(stabPh[k], dt); }
      float l, b, h; svf(stabSvf, stabC, s * 0.33f, l, b, h);
      stabHp += 0.0309f * (l - stabHp); l -= stabHp;                                // carril: pasa-altos ~220 Hz (el pad y el dron tienen lo de abajo)
      const float g = gC[C_PULSO] * run * gate * (1.0f - SC_CAP[C_PULSO] * duck) * NIV_CAP[C_PULSO] * ev * 0.55f;
      MEZCLA(C_PULSO, l * g, l * g);
    }

    // ── ARPEGIO ──
    if (envViva(arpE)) {
      const float ev = envPaso(arpE, kAtPluck, decPluck); arpFE *= decPluckF;
      if ((n & 31) == 0) arpC = svfCoef(300.0f + 1800.0f * arpFE, 1.1f);
      float s = 0; const float f2[2] = { arpF * 1.0035f, arpF * 0.9965f };
      for (int k = 0; k < 2; k++) { float dt = f2[k] * INV_SR; arpPh[k] += dt; if (arpPh[k] >= 1.0f) arpPh[k] -= 1.0f; s += (2.0f * arpPh[k] - 1.0f) - polyBlep(arpPh[k], dt); }
      float l, b, h; svf(arpSvf, arpC, s * 0.5f, l, b, h);
      arpHp += 0.0415f * (l - arpHp); l -= arpHp;                                   // carril: pasa-altos ~300 Hz
      const float g = gC[C_ARP] * run * gate * (1.0f - SC_CAP[C_ARP] * duck) * NIV_CAP[C_ARP] * ev * 0.5f;
      MEZCLA(C_ARP, l * g * agl, l * g * agr);
    }

    // ── HATS ──
    if (hatE.v > 0.0f) {
      const float ev = envPaso(hatE, 1.0f, hatAb ? decHatAb : decHat);
      float l, b, h; svf(hatSvf, hatC, ruido(), l, b, h);
      const float g = gC[C_HATS] * run * gate * NIV_CAP[C_HATS] * ev * hatAmp * hatC.k * 0.55f;
      MEZCLA(C_HATS, b * g * hatGL, b * g * hatGR);
    }

    // ── BOMBO ──
    if (kickEnv > 0.0f || kickPend) {
      if (kickPend) {                                                         // fast-kill de ~3 ms, luego el golpe nuevo
        kickEnv *= kEnvRap; kickEnv2 *= kEnvRap;
        if (kickEnv < 0.01f) { kickPend = false; kickPh = 0.0f; kickEnv = 1.0f; kickEnv2 = 1.0f; kickMall = 1.0f; kickPit = 1.0f; }
      } else { kickEnv *= kKickA; kickEnv2 *= kKickA2; if (kickEnv < ENV_FIN) kickEnv = 0.0f; }
      kickMall *= kKickM; kickPit *= kKickP;
      const float f = kickF * (1.0f + 1.8f * kickPit);                         // barrido de 2.8× a la fundamental
      kickPh += f * INV_SR; if (kickPh >= 1.0f) kickPh -= 1.0f;
      float h2 = kickPh * 2.0f; h2 -= (float)(int)h2;
      const float body = softSat((sinT(kickPh) * kickEnv + 0.42f * sinT(h2) * kickEnv2) * 1.2f) * 0.80f;   // UNA parcial fuerte: aquí sí se satura
      float l, b, h; svf(kickSvf, kickC, ruido(), l, b, h);
      const float mall = b * kickC.k * kickMall * 0.30f;                      // el mazo es RUIDO en banda, nunca un seno
      const float g = gC[C_BOMBO] * run * gate * NIV_CAP[C_BOMBO] * (body + mall);
      MEZCLA(C_BOMBO, g, g);
    }

    // ── SINTE ──
    if (envViva(snE) || envViva(snE2) || snHold > 0) {
      float y = 0;
      if (snTipo == 0) {                                                       // SONAR
        const float e1 = envPaso(snE, kAtSonar, decSonar), e2 = envPaso(snE2, kAtSonar, decSonar2);
        snPh1 += snF * INV_SR; if (snPh1 >= 1.0f) snPh1 -= 1.0f;
        snPh2 += snF * 2.0f * INV_SR; if (snPh2 >= 1.0f) snPh2 -= 1.0f;
        y = sinT(snPh1) * e1 + 0.22f * sinT(snPh2) * e2;
      } else if (snTipo == 1) {                                                // CAMPANA (FM, razón 3.5)
        const float ev = envPaso(snE, kAtBell, decBell); snEI.v *= decBellI;
        snPh1 += snF * INV_SR; if (snPh1 >= 1.0f) snPh1 -= 1.0f;
        snPhM += snF * 3.5f * INV_SR; if (snPhM >= 1.0f) snPhM -= 1.0f;
        float p = snPh1 + 1.7f * snEI.v * 0.15915f * sinT(snPhM); p -= floorf(p);
        y = sinT(p) * ev * 0.8f;
      } else {                                                                 // CORO
        if (snHold > 0) { snHold--; snE.v += (1.0f - snE.v) * kAtCoro; }
        else if (!snE.rapido) { snE.v *= decCoro; if (snE.v < ENV_UMBRAL) snE.rapido = true; }
        else { snE.v *= kEnvRap; if (snE.v < ENV_FIN) snE.v = 0.0f; }
        snF += (snFT - snF) * kSnGl;
        const float dt = snF * INV_SR; snPh1 += dt; if (snPh1 >= 1.0f) snPh1 -= 1.0f;
        const float s = (2.0f * snPh1 - 1.0f) - polyBlep(snPh1, dt);
        float l1, b1, h1, l2, b2, h2; svf(snF1, snC1, s, l1, b1, h1); svf(snF2, snC2, s, l2, b2, h2);
        y = (b1 * snC1.k + 0.6f * b2 * snC2.k) * snE.v * 2.2f;
      }
      snHp += 0.0284f * (y - snHp); y -= snHp;                                  // carril: pasa-altos ~200 Hz
      const float g = gC[C_SINTE] * run * gate * (1.0f - SC_CAP[C_SINTE] * duck) * NIV_CAP[C_SINTE] * 0.5f;
      MEZCLA(C_SINTE, y * g * sgl, y * g * sgr);
    }
#undef MEZCLA

    // ── ESPACIO: delay ping-pong (la entrada va al canal L: rebota L→R→L) ──
    if (dlyCruce) { dlyXf += kDlyXf; if (dlyXf >= 1.0f) { dlyXf = 0.0f; dlyTA = dlyTB; dlyCruce = false; } }
    int iA = dlyW - dlyTA; if (iA < 0) iA += DELAY_MAX; int iB = dlyW - dlyTB; if (iB < 0) iB += DELAY_MAX;
    const float rdL = (dlyBufL[iA] * (1.0f - dlyXf) + dlyBufL[iB] * dlyXf) * (1.0f / 32768.0f);
    const float rdR = (dlyBufR[iA] * (1.0f - dlyXf) + dlyBufR[iB] * dlyXf) * (1.0f / 32768.0f);
    dlyLpL += kDlyLp * (softCub(rdR) - dlyLpL); dlyLpR += kDlyLp * (softCub(rdL) - dlyLpR);   // eco oscuro, con saturación DENTRO del lazo
    float wL = 0.5f * (sdL + sdR) * dlyEn + DLY_FB * dlyLpL, wR = DLY_FB * dlyLpR;
    wL = wL > 0.999f ? 0.999f : wL < -0.999f ? -0.999f : wL; wR = wR > 0.999f ? 0.999f : wR < -0.999f ? -0.999f : wR;
    dlyBufL[dlyW] = (int16_t)(wL * 32767.0f); dlyBufR[dlyW] = (int16_t)(wR * 32767.0f);
    if (++dlyW >= DELAY_MAX) dlyW = 0;

    // ── ESPACIO: reverb (4 líneas con matriz de Householder + 2 difusores) ──
    float rin = (0.5f * (srL + srR) + 0.35f * 0.5f * (rdL + rdR) * dlyEn) * rvIn;
    { float y = rin - 0.6f * apBuf1[apI1]; float o = apBuf1[apI1] + 0.6f * y; apBuf1[apI1] = y; if (++apI1 >= 142) apI1 = 0; rin = o; }
    { float y = rin - 0.6f * apBuf2[apI2]; float o = apBuf2[apI2] + 0.6f * y; apBuf2[apI2] = y; if (++apI2 >= 107) apI2 = 0; rin = o; }
#define RVL(i) rvLp[i] += kRvDamp * (rvBuf[i][rvIdx[i]] - rvLp[i]);
#define RVW(i) { rvBuf[i][rvIdx[i]] = rin + rvG[i] * (rvLp[i] - sum); if (++rvIdx[i] >= RV_LEN[i]) rvIdx[i] = 0; }
    RVL(0) RVL(1) RVL(2) RVL(3)
    const float sum = 0.5f * (rvLp[0] + rvLp[1] + rvLp[2] + rvLp[3]);              // matriz de Householder
    RVW(0) RVW(1) RVW(2) RVW(3)
#undef RVL
#undef RVW
    const float rvL = (rvLp[0] + rvLp[2]) * REV_MIX, rvR = (rvLp[1] + rvLp[3]) * REV_MIX;

    float xl = dL + rdL * DLY_MIX + rvL, xr = dR + rdR * DLY_MIX + rvR;

    // ── FILTRO (BTN2 / POT4): los pesos se cruzan en 25 ms; apagado, no se calcula ──
    if (filtroM || wLp + wHp + wBp > 0.0005f) {
      if (!filtAct) { mfL.c1 = mfL.c2 = mfR.c1 = mfR.c2 = 0.0f; filtAct = true; }       // al encender arranca limpio (el estado viejo ya no vale)
      mfC.a1 += mfD.a1; mfC.a2 += mfD.a2; mfC.a3 += mfD.a3; mfC.k += mfD.k;
      float l1, b1, h1, l2, b2, h2; svf(mfL, mfC, xl, l1, b1, h1); svf(mfR, mfC, xr, l2, b2, h2);
      const float dry = 1.0f - wLp - wHp - wBp;
      xl = dry * xl + wLp * l1 + wHp * h1 + wBp * mfC.k * b1; xr = dry * xr + wLp * l2 + wHp * h2 + wBp * mfC.k * b2;
    } else filtAct = false;

    xl *= volC; xr *= volC;

    // ── LIMITADOR suave (sólo actúa por encima de 0.75) ──
    xl = limSuave(xl); xr = limSuave(xr);

    // ── techo de agudos (13 kHz) DESPUÉS del limitador, y bloqueador de continua ──   (la saturación va ANTES del filtro maestro y de este techo)
    { float y = lpb0 * xl + lpb1 * lpx1L + lpb2 * lpx2L - lpa1 * lpy1L - lpa2 * lpy2L; lpx2L = lpx1L; lpx1L = xl; lpy2L = lpy1L; lpy1L = y; xl = y; }
    { float y = lpb0 * xr + lpb1 * lpx1R + lpb2 * lpx2R - lpa1 * lpy1R - lpa2 * lpy2R; lpx2R = lpx1R; lpx1R = xr; lpy2R = lpy1R; lpy1R = y; xr = y; }
    { float y = xl - dcxL + 0.9958f * dcyL; dcxL = xl; dcyL = y; xl = y; }
    { float y = xr - dcxR + 0.9958f * dcyR; dcxR = xr; dcyR = y; xr = y; }
    if (!(xl == xl)) xl = 0.0f; if (!(xr == xr)) xr = 0.0f;                    // red de seguridad contra NaN
    xl = xl > 0.98f ? 0.98f : xl < -0.98f ? -0.98f : xl; xr = xr > 0.98f ? 0.98f : xr < -0.98f ? -0.98f : xr;
    salida[n * 2] = (int16_t)(xl * 32700.0f); salida[n * 2 + 1] = (int16_t)(xr * 32700.0f);
  }
  arpGL = aEL; arpGR = aER; snGL = sEL; snGR = sER;
  pubGate = gate;

  const uint32_t t1 = micros();                  // la carga se mide ANTES de escribir: i2s_channel_write bloquea esperando al DMA y eso no es trabajo
  size_t w = 0;
  i2s_channel_write(tx_chan, salida, sizeof(salida), &w, portMAX_DELAY);
  const float carga = (float)(t1 - t0) * (SR / ((float)BUFFER_SAMPLES * 1.0e6f));   // fracción del tiempo del bloque
  pubCarga = pubCarga * 0.9f + carga * 0.1f;
  // REGULADOR DE CARGA (un bache de CPU se oye como click): sobre el 80 % durante ~1 s sube un nivel de aligeramiento
  //   nivel 1 = el pad deja de doblar sierras (raíz y quinta), nivel 2 = además calla la textura.
  // Por debajo del 55 % durante ~3 s baja un nivel. Cada cambio llega con rampa, sin saltos.
  static uint16_t cuentaAlta = 0, cuentaBaja = 0;
  if (carga > pubCargaMax) pubCargaMax = carga;                // el control lo pone a 0 cada 2 s
  if (pubCarga > 0.80f) { cuentaBaja = 0; if (++cuentaAlta > 340 && aligera < 2) { aligera++; cuentaAlta = 0; } }
  else if (pubCarga < 0.55f) { cuentaAlta = 0; if (++cuentaBaja > 1000 && aligera > 0) { aligera--; cuentaBaja = 0; } }
  else { cuentaAlta = 0; cuentaBaja = 0; }
  padDoble = (aligera == 0); pubAligera = (uint8_t)aligera;
}

// ==============================================================================================================================================
// CONTROL (core 0): botones, pots, LEDs y guardado
// ==============================================================================================================================================
CRGB leds[NUM_LEDS];
Preferences prefs;
Guardado ultimoGuardado;
float potS[4] = { 0.5f, 0.0f, 0.0f, 0.5f };
bool  botPrev[5] = { true, true, true, true, true };
unsigned long botT[5] = { 0, 0, 0, 0, 0 };
unsigned long tCambio = 0, tLed = 0; uint16_t ultSeq = 0; bool jugAntes = false; uint32_t tick = 0; int zonaPot3 = 0;

uint16_t sumaGuardado(const Guardado &g) { uint16_t s = 0x5A5A; const uint8_t *p = (const uint8_t *)&g; for (size_t i = 0; i < sizeof(g); i++) if (i < 4 || i >= 6) s = (uint16_t)(s * 31 + p[i]); return s; }
Guardado escenaActual() {
  Guardado g; memset(&g, 0, sizeof(g));
  g.magia = 0xA7; g.version = 1; g.tonica = pubTonica; g.modo = pubModo; g.bpm = pubBpm; g.filtro = pubFiltro; g.delay = pubDelay;
  g.pausa = pubPausa; g.sinte = pubSinte; g.semilla = pubSemilla; g.suma = sumaGuardado(g);
  return g;
}
void guardar() { Guardado g = escenaActual(); prefs.putBytes("esc", &g, sizeof(g)); ultimoGuardado = g; tCambio = 0; }
bool cargar() {
  Guardado g;
  if (prefs.getBytesLength("esc") != sizeof(g)) return false;
  prefs.getBytes("esc", &g, sizeof(g));
  if (g.magia != 0xA7 || g.version != 1 || g.suma != sumaGuardado(g)) return false;
  if (g.modo >= NUM_MODOS || g.tonica > 11 || g.bpm < (uint8_t)BPM_MIN || g.bpm > (uint8_t)BPM_MAX || g.filtro > 3 || g.delay > 3 || g.pausa > 3 || g.sinte > 1 || !g.semilla) return false;
  tonica = g.tonica; modo = g.modo; bpm = g.bpm; filtroM = g.filtro; delayM = g.delay; pausaM = g.pausa; sinteOn = g.sinte; semilla = g.semilla;
  ultimoGuardado = g; return true;
}

float leerPot(int i) {
  uint32_t s = 0; for (int k = 0; k < 4; k++) s += analogRead(POT_PIN[i]);
  float v = ((float)s * 0.25f / 4095.0f - 0.02f) / 0.96f;                    // los extremos se alcanzan de verdad
  return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}
int zonaDe(float v, int cur) {                                                // 6 zonas con histéresis: el ADC no puede titilar entre dos
  int t = (int)(v * 6.0f); if (t > 5) t = 5;
  while (t > cur && v >= (float)(cur + 1) / 6.0f + 0.012f) cur++;
  while (t < cur && v <= (float)cur / 6.0f - 0.012f) cur--;
  return cur;
}

void renderLEDs() {
  static const uint8_t col[NUM_MODOS][3] = { { 120, 40, 255 }, { 0, 190, 200 }, { 255, 60, 20 } };
  const uint8_t *c = col[pubModo % NUM_MODOS];
  const bool cargaAlta = pubCarga > 0.80f;
  const float tenue = pubGate > 0.5f ? 1.0f : 0.25f;
  const int capa[NUM_LEDS] = { C_DRON, C_PAD, C_TEXT, C_ARP, C_HATS, -1 };
  for (int i = 0; i < NUM_LEDS; i++) {
    float niv = 0.06f, fl = 0.0f;
    if (i == 2) { niv += 0.5f * fmaxf(pubGan[C_TEXT], pubGan[C_PULSO]); fl = fmaxf(pubFlash[C_TEXT], pubFlash[C_PULSO]); }
    else if (i == 3) { niv += 0.5f * fmaxf(pubGan[C_ARP], pubGan[C_SINTE]); fl = fmaxf(pubFlash[C_ARP], pubFlash[C_SINTE]); }
    else if (i == 4) { niv += 0.5f * fmaxf(pubGan[C_HATS], pubGan[C_BOMBO]); fl = pubFlash[C_HATS]; }
    else if (i == 5) { fl = pubFlash[5]; niv = pubJugando ? 0.05f : 0.02f; }
    else { niv += 0.5f * pubGan[capa[i]]; fl = pubFlash[capa[i]]; }
    float k = (niv + 0.8f * fl) * tenue; if (k > 1.0f) k = 1.0f;
    if (i == 5 && cargaAlta) leds[i] = CRGB(255, 0, 255);
    else leds[i] = CRGB((uint8_t)(c[0] * k), (uint8_t)(c[1] * k), (uint8_t)(c[2] * k));
  }
  for (int i = 0; i < N_CAPAS; i++) pubFlash[i] = pubFlash[i] * 0.80f;       // los destellos se apagan solos
  FastLED.show();
}

void pasoControl() {
  const unsigned long t = millis();
  for (int b = 0; b < 5; b++) {                                              // botones: el flanco de presión, sin ventanas de espera
    bool now = digitalRead(BTN_PIN[b]);
    if (now == LOW && botPrev[b] == HIGH && (t - botT[b]) > DEBOUNCE_MS) {
      botT[b] = t;
      switch (b) { case 0: reqPlay = reqPlay + 1; break; case 1: reqFiltro = reqFiltro + 1; break; case 2: reqSinte = reqSinte + 1; break; case 3: reqDelay = reqDelay + 1; break; case 4: reqPausa = reqPausa + 1; break; }
    }
    botPrev[b] = now;
  }
  const int i = (int)(tick++ & 3);                                           // un pot por tick (cada uno se lee cada 4 ms)
  potS[i] += (leerPot(i) - potS[i]) * 0.35f;
  pVol = potS[0]; pMuta = potS[1]; pAtm = potS[2]; pCorte = potS[3];
  zonaPot3 = zonaDe(potS[2], zonaPot3); pEtapa = (uint8_t)zonaPot3;

  // ── guardado: al sortear una escena nueva y al parar; los cambios de modo, 3 s después de dejar de tocar y con la música detenida ──
  // (escribir en flash mientras suena puede dejar al DMA sin datos: por eso no se escribe en pleno Play)
  const bool jug = pubJugando;
  if (pubEscenaSeq != ultSeq) { ultSeq = pubEscenaSeq; guardar(); }
  else if (jugAntes && !jug) guardar();
  else if (!jug && (escenaCargada || pubEscenaSeq != 0)) {     // nunca se guarda la escena por defecto: nadie la sorteó
    Guardado g = escenaActual();
    if (memcmp(&g, &ultimoGuardado, sizeof(g)) != 0) { if (!tCambio) tCambio = t ? t : 1; else if (t - tCambio > 3000) guardar(); }
    else tCambio = 0;
  }
  jugAntes = jug;

  if (t - tLed >= 25) { tLed = t; renderLEDs(); }
#if DIAG
  static unsigned long tDiag = 0;
  if (t - tDiag >= 2000) { tDiag = t; Serial0.printf("CPU audio: prom %.0f %%  max %.0f %%  | regulador nivel %d | jugando %d\n", pubCarga * 100.0f, pubCargaMax * 100.0f, (int)pubAligera, (int)pubJugando); pubCargaMax = 0.0f; }
#endif
}

// ==============================================================================================================================================
// SETUP
// ==============================================================================================================================================
void i2s_init() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  chan_cfg.dma_desc_num  = 6;
  chan_cfg.dma_frame_num = 256;      // ≈ 35 ms de colchón: es un ambiente, la latencia no importa y el colchón protege de cualquier bache
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)I2S_BCK, .ws = (gpio_num_t)I2S_LCK, .dout = (gpio_num_t)I2S_DIN, .din = I2S_GPIO_UNUSED,
                  .invert_flags = { false, false, false } },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

#ifndef SIMULADOR
void audioTask(void *);
void controlTask(void *);
#endif

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  for (int b = 0; b < 5; b++) pinMode(BTN_PIN[b], INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
#if DIAG
  Serial0.begin(115200);
#endif
  prepararSintesis();
  prefs.begin("atmosf", false);
  escenaCargada = cargar();
  if (!escenaCargada) { tonica = 2; modo = 0; bpm = 118; semilla = 1; }
  for (int i = 0; i < 4; i++) { delay(2); potS[i] = leerPot(i); }
  pVol = potS[0]; pMuta = potS[1]; pAtm = potS[2]; pCorte = potS[3];
  zonaPot3 = zonaDe(potS[2], (int)(potS[2] * 6.0f > 5.0f ? 5 : potS[2] * 6.0f)); pEtapa = (uint8_t)zonaPot3; etapa = zonaPot3; nNotas = etapa + 2;
  sps = SR * 60.0f / ((float)bpm * 4.0f);
  construirEscena(); reiniciarMusica(); aplicarDelay(); dlyTA = dlyTB; dlyCruce = false;
  publicarEstado(); pubJugando = 0;
  ultSeq = pubEscenaSeq;

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHT);
  FastLED.clear(); FastLED.show();

  i2s_init();
#ifndef SIMULADOR
  // El audio tiene el core 1 para él solo; botones, pots y LEDs van al core 0. Así ni el ADC ni FastLED.show()
  // pueden dejar al DAC sin datos.
  xTaskCreatePinnedToCore(audioTask,   "audio",   8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 4096, NULL,  3, NULL, 0);
#endif
}

#ifndef SIMULADOR
void audioTask(void *)   { for (;;) renderBuffer(); }                       // core 1, prioridad 10
void controlTask(void *) { for (;;) { pasoControl(); vTaskDelay(1); } }     // core 0, 1 kHz
void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
#else
void loop() {}
#endif
