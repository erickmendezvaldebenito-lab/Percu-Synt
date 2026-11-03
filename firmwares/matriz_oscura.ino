// ==============================================================================================================================================
// PERCU-SYNTH — MATRIZ OSCURA (8 estilos oscuros y agresivos · un panel por instrumento · samples dentro del firmware · matriz 32×8) — GC Lab Chile
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
// - 6 LEDs WS2812 SMD internos de la placa |DATA -> 46| (índices 0..5, indicadores de estado)
// - Matriz WS2812 de 32×8 (256 LEDs) encadenada DESPUÉS de los 6 SMD, mismo pin |DATA -> 46|
//   (índices 6..261 · fuente de 5 V propia recomendada, GND común con la placa)
// - 5 Botones con pull-up |BTN1 -> 44, BTN2 -> 42, BTN3 -> 0, BTN4 -> 45, BTN5 -> 47|
// - 4 Potenciómetros analógicos |POT1 -> ADC1, POT2 -> ADC2, POT3 -> ADC8, POT4 -> ADC10|
// - USB (el mismo del Arduino IDE): datos de cada instrumento para las proyecciones (Web Serial)
// ==============================================================================================================================================
// ARDUINO IDE — settings críticos
// ==============================================================================================================================================
// - Board              : ESP32S3 Dev Module
// - USB CDC On Boot    : Enabled      (las proyecciones leen por este puerto)
// - Flash Mode         : DIO          (¡OPI rompe I2S!)
// - Partition Scheme   : Huge APP (3MB No OTA/1MB SPIFFS)   (los 39 samples van en la flash: ~2.2 MB)
// - PSRAM              : no hace falta
// ==============================================================================================================================================
// LIBRERÍAS REQUERIDAS
// ==============================================================================================================================================
// - ESP32 Arduino core ≥ 3.x (incluye driver/i2s_std.h)
// - FastLED (gestor de librerías de Arduino)
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// El sucesor oscuro de industrial_matriz_v2: la misma banda en la placa (batería, bajo, melodías y un
// oscilador para tocar encima), con los mismos paneles y la misma lógica, pero con 8 ESTILOS AGRESIVOS,
// timbres nuevos, melodías siempre oscuras y un sexto panel que dispara SAMPLES: un NOTICIERO
// DEL FUTURO que se glitchea mientras informa (24 frases con voces de ElevenLabs y el glitch horneado) que va
// DENTRO del firmware, en samples.h — se comparte y se carga tal cual, sin tarjeta.
//
//   0 INDUSTRIAL   Mi eólico, Em ×6 · C ×2 — el INDUSTRIAL del v2, con sonidos y patrones nuevos
//   1 PSYTRANCE    Mi frigio — bombo-bajo rodando, ácido que se abre
//   2 PSYCORE      Fa# doble armónica (árabe) — la tónica contra la b2, FM que chilla
//   3 ELECTRO DARK Fa frigio — F5 · F5 · Gb5 · F5, bajo sucio sincopado, sync que rasga
//   4 INDUSTRIAL DURO  Re frigio dominante (hijaz), un solo acorde — martillos y pistones, nota de ruido hipnótica
//   5 CYBERPUNK    Do# dórico — C#m B F# G#m, caja a medio tiempo, bajo de octavas
//   6 TRAP         Fa dórico — Fm Ab Eb Bb, 808 con glide, hats con redobles, pluck oscuro
//   7 ROCK INDUSTRIAL  Mi frigio dominante — batería de rock, riff de quintas saturadas
//
// En cada estilo el PATRÓN 1 y el timbre que trae son los que se eligieron escuchando los ejemplos
// (matriz_oscura/ejemplos/); los otros tres patrones y los demás timbres son variaciones en esa
// dirección. Todo se escribe en GRADOS de la escala y relativo al acorde que suena: ningún botón puede
// sacar una nota de tono ni un golpe de tiempo. La distorsión es por voz y moderada, siempre antes del
// filtro: agresivo, pero que cada sonido se entienda.
//
// MATRIZ 32×8: 11 escenas (las 8 del v1 + INTERFERENCIA · CUADRADOS · RAYOS, que se mueven a saltos
// en el pulso), sorteadas cada 4 compases, en cada Play y en cada cambio de estilo. Al encender,
// 3 s de PRUEBA DE ORIENTACIÓN (ver README).
//
// ARQUITECTURA: audio en su propia tarea en el CORE 1; botones, pots y USB en una tarea a 1 kHz, la
// matriz en otra, las dos en el CORE 0. Los controles sólo dejan
// PEDIDOS que el audio aplica en el borde de un buffer.
// ==============================================================================================================================================
// FUNCIONAMIENTO — 6 PANELES. Un botón = una función, en el flanco de presión.
// ==============================================================================================================================================
// EN TODOS LOS PANELES:  BTN4 → siguiente panel (General → Bajo → Batería → Melodías → Oscilador → Samples)
//                        BTN5 → break (calla bombo y bajo) / vuelta del break. MANTENIDO más de 2 s:
//                               SUBIDA de 16 compases que termina en un golpe y espera en silencio;
//                               el próximo toque del BTN5 es el DROP (todo entra desde el "1")
//
//   Al cambiar de panel cada pot queda "suelto" y no cambia nada hasta que lo muevas un poco (sin
//   saltos). El LED 5 dice el panel: verde General · rojo Bajo · ámbar Batería · cian Melodías ·
//   azul Oscilador · violeta Samples. Las secuencias corren siempre; para callar un instrumento, su volumen (POT1).
//
// GENERAL     BTN1 play/stop (arranca en el "1") · BTN2 estilo (siguiente de los 8) · BTN3 fill al final del compás
//             POT1 volumen · POT2 tempo (70–175) · POT3 swing (recto → 70 %) · POT4 pasa-bajos general
// BAJO        BTN1 distorsión sí/no · BTN2 patrón (4 por estilo) · BTN3 timbre (RODANTE · SUCIO · LIMPIO · 808 · ÁCIDO · PULSO)
//             POT1 volumen · POT2 filtro · POT3 resonancia · POT4 caída (40 ms – 1.2 s; el 808 ×3.5)
// BATERÍA     BTN1 percusión extra: 1 → 2 → 3 pistas → nada (recortes cortos, medios y largos de los samples,
//             en los huecos del patrón) · BTN2 patrón (4 por estilo) · BTN3 kit (INDUSTRIAL · PSY · DURO · ROCK · TRAP)
//             POT1 volumen · POT2 filtro · POT3 largo de los golpes · POT4 DENSIDAD: abajo del todo
//             la batería calla salvo el bombo, que no depende de ella
// MELODÍAS    BTN1 octava (normal · +1 · −1) · BTN2 patrón (4 por estilo) · BTN3 timbre (ÁCIDO · FM · STAB · SYNC · PLUCK · QUINTAS)
//             POT1 volumen · POT2 filtro · POT3 resonancia · POT4 caída
// OSCILADOR   BTN1 encender/apagar · BTN2 timbre (GRUÑIDO · SIERRAS · FM · CUADRADAS) · BTN3 octava (normal · +1 · −1)
//             POT1 NOTA de la escala del estilo. Son 15 grados, de una octava abajo a una arriba de la
//             tónica (al centro) · POT2 filtro · POT3 profundidad del LFO del filtro · POT4 VOLUMEN.
//             Un compresor propio lo deja parejo aunque cambien el filtro o el timbre; el envío al delay es fijo
// SAMPLES     BTN1 dispara un TITULAR · BTN2 al PRESENTADOR · BTN3 una SEÑAL CAÍDA (la más rota) — cada uno
//             al azar de sus 8 frases, nunca la misma dos veces seguidas
//             POT1 volumen · POT2 pitch (2 octavas abajo … 1 arriba; al centro, como está) · POT3 reverb · POT4 eco
//             Suenan también con la música detenida
//
// LEDs de la placa: 0 bombo · 1 bajo · 2 hats · 3 caja/perc/clap/tom · 4 melodías y samples — un instrumento
// callado se ve en rojo tenue. LED 5 = color del panel con el pulso de cada negra; MAGENTA si un
// bloque de audio usa más del 80 % de su tiempo. Con BREAK, 0 y 1 en rojo fijo.
//
// MODO DE USO:
// 1. En el panel General (LED 5 verde) aprieta BTN1: suenan batería, bajo y melodías. POT1 es
//    el volumen y POT2 el tempo.
// 2. BTN2 cambia de estilo (trae su tempo sugerido en el README; el tempo lo pones tú con el POT2).
//    BTN3 mete un fill al final del compás.
// 3. BTN4 pasa al panel siguiente (Bajo, Batería, Melodías, Oscilador, Samples). En cada uno, BTN2 cambia
//    el patrón y BTN3 el timbre o el kit; BTN1 distorsiona el bajo, suma percusión o cambia la octava.
// 4. El oscilador parte apagado: en su panel, BTN1 lo enciende y el POT1 elige la nota.
// 5. En Samples, BTN1 dispara un golpe, BTN2 una voz y BTN3 un FX.
// 6. BTN5 hace un break. Mantenido más de 2 s viene la subida; el próximo toque es el drop.
// ==============================================================================================================================================

// El core de Arduino compila con -Os (tamaño). Este sketch es casi todo bucles de audio: con O2
// (velocidad) el mismo código usa bastante menos CPU por bloque (medido en industrial_matriz).
#pragma GCC optimize ("O2")
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <FastLED.h>
#include <math.h>
#include <string.h>

// ─── Tipos (arriba del todo para que el IDE de Arduino genere bien los prototipos) ───
struct Filtro { float ic1, ic2, a1, a2, a3, k; };              // SVF de Zavalishin (TPT)
struct BQ     { float b0, b1, b2, a1, a2, z1, z2; };           // biquad RBJ (DF2 transpuesta)
struct VozBombo { bool viva; float f, fObj, fCoef, ph, env, knock, click, eC, kC, cC, amp, punch, kAmt, cAmt, sat; BQ bp, hp; };
struct VozInd {
  bool  viva;
  float amp, gL, gR;
  float ph[2], inc[2], a[2];         // capa TONAL: hasta dos parciales
  float env, eC;
  float caida, caidaC, cuanto;       // caída de pitch del impacto
  float nz, nC, nAmt;                // capa de RUIDO con su propia envolvente
  float fc0, fc1, barre, barreInc, q;
  Filtro s;
  BQ    banda;                       // pasa-altos de carril: nadie le pisa el grave al bombo
  bool  conBanda;                    // los de puro ruido no lo necesitan: su pasa-banda ya saca el grave
};
struct VozPluck { bool viva, atk; int nota; float f1, inc1, inv1, amp, fenv, base, oct, q, eC, gL, gR; Filtro s; };   // arpegio
struct Figura { bool viva; float cx, cy, r, vr, brillo; uint8_t tipo, hue; };   // escena FIGURAS
struct SegAc { int8_t grado; uint8_t compases; };              // un acorde de la progresión y cuántos compases dura

// Un golpe de percusión: TODO lo que define su sonido. El motor (renderInd) es uno solo; un kit es
// una tabla de 6 de éstos + los números del bombo.
struct Golpe {
  float amp;
  float f, r2, a0, a1;               // capa tonal: frecuencia, razón de la 2.ª parcial (0 = sin), niveles (a0 = 0: sin capa tonal)
  float eDec, cuanto, caidaDec;      // caída del tono (tau), cuánto cae el pitch y en qué tau
  float nAmt, nDec, nAcc;            // ruido: nivel, tau normal y tau con acento
  float fc0, fc1, barre, q;          // pasa-banda del ruido: de fc0 a fc1 en `barre` s
  float hp;                          // pasa-altos de carril (0 = sin)
  float pan, panRnd;
};
struct Kit {
  const char *nombre;
  // BOMBO en tres bandas (drum_poder): dónde aterriza la fundamental, desde cuánto más arriba
  // parte y en cuánto cae, caída, nivel del 2.º armónico (el PECHO), mazo de RUIDO pasa-banda,
  // click, y saturación propia (el bombo tiene UNA parcial fuerte: acá sí se puede saturar).
  float kFreq, kRatio, kDrop, kDec, kPunch, kKnock, kKnockF, kClick, kSat;
  Golpe g[6];
};
struct TimbreBajo { const char *nombre; float sierras, detune, sub, seno, drive, envOct, qMul, fenvRel, nivel, decMul; bool cuadrada; };
struct TimbreMel  { const char *nombre; uint8_t onda; float corte, envOct, q, fenvTau, nivel, ruido, drive, det; bool banda; };
#define NUM_SEG 4
struct Estilo {
  const char *nombre;
  uint8_t hue;
  int8_t  transp;                    // semitonos sobre La
  uint8_t escala;
  SegAc   prog[NUM_SEG];
  uint8_t kit, tBajo, tMel, tOsc;    // timbres que trae el estilo (después se cambian en cada panel)
  int8_t  octKeys;                   // octavas de las melodías respecto de Do4 (el riff del rock va dos abajo)
  const char *bajo[4][2];            // 4 patrones: compás A (tres veces) y B (el 4.º)
  const char *bateria[4][7];         // 4 patrones: bombo + las 6 pistas, un compás de 16
  const char *fill[7];               // los 4 últimos pasos del compás, bombo + 6 pistas
  const char *keys[4];               // 4 patrones de melodía: 2 compases (32)…
  const char *arp[4];                // …y la figura del arpegio que va con cada uno (16)
};
// La parte de los controles que el audio lee entera al borde de cada buffer (el combo la repone)
struct Ctl {
  bool tocar, repeat, brk;
  int  estilo;
  bool drumOn, bajoOn, melOn, oscOn;
  int  patDrum, kit; bool kick, mitad;
  int  patBajo, tBajo, octBajo; bool glide;
  int  patMel, tMel, octMel; bool arp;
  int  tOsc, octOsc;                 // octOsc: 0 normal · 1 +1 octava · 2 −1 octava
  bool distBajo;                     // BTN1 del Bajo: distorsión
  int  extra;                        // BTN1 de la Batería: percusión extra hecha de recortes de samples (0 = nada … 3)
};
struct Adpcm { const uint8_t *p; int32_t pred; int idx; uint32_t n; };   // lector IMA ADPCM (n = muestras leídas)
struct VozRecorte { bool viva; int r; int32_t pos; float amp, obj, k, gL, gR, vel; };   // un golpe de la percusión extra
// Un sample sonando. Al derecho, el ADPCM se decodifica de corrido (pen y ult son las dos últimas muestras);
// al revés, se decodifica el bloque entero de 1024 que toca en buf. El μ-law se lee directo.
struct VozMuestra { bool viva, reversa, sintetica; int idx; int32_t pos; float frac, amp, obj, k, lp, t, f, ph;
                    Filtro fz; uint32_t sem; Adpcm dec; int32_t pen, ult; int bloque; int16_t buf[1024]; };
struct Redoble { int quedan; float falta, cada, vel; };   // los redobles del hat (r = 2 golpes por paso · t = 3)

// ─── I2S PCM5102 ───────────────────────────────────────────
#define I2S_LCK   39
#define I2S_DIN   40
#define I2S_BCK   41
#define SAMPLE_RATE     44100
#define BUFFER_SAMPLES  128
const float SR = (float)SAMPLE_RATE;
const float PI_F = 3.14159265f;

// ─── LEDs: 6 SMD de la placa + matriz 32×8 en el mismo pin ──
#define LED_PIN        46
#define STATUS_LEDS     6
#define MATRIZ_ANCHO   32
#define MATRIZ_ALTO     8
#define MATRIZ_LEDS   (MATRIZ_ANCHO * MATRIZ_ALTO)
#define NUM_LEDS      (STATUS_LEDS + MATRIZ_LEDS)
#define LED_BRIGHT    160
#define LED_TYPE      WS2812
#define COLOR_ORDER   GRB
#define LED_MAX_MA   2000                     // tope duro de FastLED (baja el brillo global si se pasa)

// ══════════════════════════════════════════════════════════════════════════════════════════════
// CÓMO ESTÁ CABLEADA LA MATRIZ  ← si la prueba de orientación del arranque sale mal, se toca acá
// ══════════════════════════════════════════════════════════════════════════════════════════════
const bool MATRIZ_POR_COLUMNAS = true;    // false = el dato recorre filas de 32
const bool MATRIZ_SERPENTINA   = true;    // false = todas las columnas (o filas) en el mismo sentido
const bool MATRIZ_ESPEJO_X     = false;   // invierte izquierda ↔ derecha
const bool MATRIZ_ESPEJO_Y     = false;   // invierte arriba ↔ abajo

// ══════════════════════════════════════════════════════════════════════════════════════════════
// PRESUPUESTO DE CORRIENTE DE LA MATRIZ  ← se toca si cambias la alimentación
// ══════════════════════════════════════════════════════════════════════════════════════════════
// Colgada del 5 V de la placa o del USB, un frame muy encendido hunde el riel y CORTA EL AUDIO
// (lección de bajo_8_pasos_fsr_clock). Con fuente propia de 5 V / 4 A sube MATRIZ_MAX_MA a 2500.
const float MATRIZ_MAX_MA = 450.0f;
const float MA_POR_LED    = 60.0f;
const float RECUPERA_LED  = 0.06f;
const float CARGA_MAXIMA  = MATRIZ_MAX_MA / ((float)MATRIZ_LEDS * MA_POR_LED * (LED_BRIGHT / 255.0f));
const unsigned long LED_REFRESH_MS = 20;
const unsigned long PRUEBA_MS      = 3000;
CRGB leds[NUM_LEDS];

// ─── Botones y pots ────────────────────────────────────────
const uint8_t BTN_PIN[5] = { 44, 42, 0, 45, 47 };   // BTN1..BTN5 (orden correcto de la placa)
const uint8_t POT_PIN[4] = { 1, 2, 8, 10 };
const unsigned long DEBOUNCE_MS = 25;

// ─── USB: datos para las proyecciones ───────────────────────
#ifndef LINEA_PROYECCION
#define LINEA_PROYECCION 0                  // línea `P`: detalle de cada pista (notas, timbres, estados), ver README
#endif
#define LINEA_RESONANCIA 1                  // líneas `I` y `R` de mezclador_pistas: las lee Resonancia
const unsigned long LINEA_MS = 16;          // ~60 por segundo

// ─── SAMPLES (panel 6): van DENTRO del firmware, en samples.h (lo escribe preparar_samples.py desde banco/) ───
#define NUM_BANCOS     3                            // BTN1 · BTN2 · BTN3 del panel Samples
#define VOCES_MUESTRA  2                            // la que sonaba se apaga en 4 ms al disparar otra
const char *NOMBRE_BANCO[NUM_BANCOS] = { "TITULARES", "PRESENTADOR", "SENAL" };   // el noticiero del futuro (generar_noticias.py)
#include "samples.h"
// PERCUSIÓN EXTRA (BTN1 de Batería): hasta 3 pistas hechas de RECORTES de los samples (samples.h): la 1 con
// recortes cortos (40–60 ms), la 2 medios (80–120), la 3 largos (160–260). Caen sólo donde el patrón no tiene
// bombo, caja ni clap, y con la densidad del panel; cada pista con su golpe fijo para ese estilo y patrón.
#define NUM_EXTRA      3
const float EXTRA_N[NUM_EXTRA] = { 4.0f, 3.0f, 2.0f };   // golpes por compás de cada pista con la densidad al máximo (si hay huecos)
const float EXTRA_PAN[NUM_EXTRA]  = { 0.30f, 0.70f, 0.50f };
const float NIVEL_EXTRA = 0.55f;
// DISTORSIÓN del bajo (BTN1 del Bajo): multiplica la saturación del timbre, siempre ANTES del filtro (el filtro
// se come lo áspero de arriba) y con el nivel compensado: suena más sucio, no más fuerte.
const float DIST_BAJO = 3.2f;
// ATAQUES fijos (el POT3 de Bajo y Melodías ahora es la resonancia)
const float ATK_BAJO = 0.002f, ATK_MEL = 0.003f;

// ==============================================================================================================================================
// SONIDO — lo fijo se ajusta acá
// ==============================================================================================================================================
// MEDIR 1 = diagnóstico: arranca tocando y cada 2 s imprime por Serial0 (el conector UART de la
// placa) cuánto CPU usa el audio y cuánto trabaja el limitador. Uso normal: 0.
#ifndef MEDIR
#define MEDIR 0
#endif

const float BPM_MIN = 70.0f, BPM_RANGO = 105.0f;   // POT2 del General: 70 … 175 (el psycore va a 170)
// Ganancia general con POT1 del General al máximo. Medido en la placa (MEDIR): con ×1.36 la mezcla
// llegaba a 1.6 antes del limitador, que bajaba hasta −4.7 dB con muestras en el techo = el clipeo.
// Con 0.70 ese mismo momento queda en ~0.82: el limitador sólo atrapa picos sueltos.
const float MASTER = 0.70f;
const float TECHO  = 0.89f;               // −1 dBFS: techo del limitador
#define LIM_LOOK 64
const float NIVEL_BOMBO = 0.78f;

// BAJO
const int   TONICA_BAJO = 33;             // La1 (55 Hz) + transp del estilo; abajo de Mi1 sube una octava
const float NIVEL_BAJO = 0.62f;
const float BAJO_GLIDE = 0.060f;          // portamento del BTN5 del panel del bajo (tau, s)
const float BAJO_DEC_MIN = 0.040f, BAJO_DEC_RANGO = 30.0f;    // 40 ms … 1.2 s

// SIDECHAIN del bombo: cuánto agacha a cada capa
const float SC_BAJO = 0.35f, SC_IND = 0.40f, OSC_SC = 0.15f;
const float NIVEL_IND = 1.25f;
// SAMPLES (panel 6): nivel con el POT1 al máximo, y cuánto manda a la reverb y al eco con POT3/POT4 al máximo
const float NIVEL_MUESTRA = 1.00f, MUESTRA_REV_MAX = 1.0f, MUESTRA_ECO_MAX = 0.8f;   // con 0.55 un sample quedaba 10 dB bajo la mezcla; con 1.3 y todo al máximo el limitador bajaba 6 dB
const float IMPACTO_S = 1.6f;                // el impacto sintetizado: sólo si una categoría quedara vacía en samples.h
// FM de las melodías (timbre FM): índice en vueltas de fase (0.72 ≈ 4.5 rad) y realimentación del modulador
const float MEL_FM_IDX = 0.72f, MEL_FM_FB = 0.10f;

// ══ ARMONÍA ══ Todo se escribe en GRADOS de la escala: ninguna nota puede salir de tono.
// Escalas OSCURAS: eólico, frigio, dos ÁRABES (frigio dominante = hijaz, y doble armónica, con sus dos
// segundas aumentadas) y dórico. Los acordes se apilan en terceras de la escala TAL CUAL (no se cambian por
// el consonante más cercano); las progresiones sólo usan grados con tríada consonante en su escala.
#define NUM_MODOS 5
#define M_EOLICO   0
#define M_FRIGIO   1
#define M_FRIGDOM  2
#define M_DOBLE    3
#define M_DORICO   4
const int ESCALAS[NUM_MODOS][7] = {
  { 0, 2, 3, 5, 7, 8, 10 },   // 0 eólico (menor natural)
  { 0, 1, 3, 5, 7, 8, 10 },   // 1 frigio (segunda menor)
  { 0, 1, 4, 5, 7, 8, 10 },   // 2 frigio dominante / hijaz (b2 y tercera mayor: la segunda aumentada)
  { 0, 1, 4, 5, 7, 8, 11 },   // 3 doble armónica, la «árabe» (b2, 3, b6, 7: dos segundas aumentadas)
  { 0, 2, 3, 5, 7, 9, 10 },   // 4 dórico (menor con la sexta mayor: el IV es mayor)
};

// ══ TIMBRES DEL BAJO (BTN3 del panel del bajo) ══
//   sierras: nivel de las dos sierras (detune = razón entre ellas; 1 = una sola) · sub: cuadrada una
//   octava abajo · seno: fundamental redonda + su octava (UNA parcial fuerte: se puede saturar) ·
//   drive · envOct: cuánto abre el filtro cada nota · qMul: resonancia · fenvRel: el filtro se
//   cierra en esta fracción de la caída (más chico = más "cuac")
// decMul multiplica la caída del POT4 (el 808 es largo) · cuadrada = el oscilador del frente es una cuadrada
// en vez de una sierra (el sub una octava abajo es siempre una cuadrada; el seno va a la fundamental)
#define NUM_TBAJO 6
#define TB_RODANTE 0
#define TB_SUCIO   1
#define TB_LIMPIO  2
#define TB_808     3
const TimbreBajo TBAJO[NUM_TBAJO] = {
  //  nombre       sierras detune    sub    seno  drive envOct qMul  fenvRel nivel decMul cuadrada
  { "RODANTE",     1.0f, 1.0f,     0.0f,  0.50f, 2.00f, 2.6f, 1.1f, 0.30f, 1.00f, 1.0f, false },   // el del psy: corto, el filtro se cierra al tiro
  { "SUCIO",       1.0f, 1.0f,     0.45f, 0.0f,  2.40f, 3.0f, 1.4f, 0.55f, 0.95f, 1.0f, false },   // sierra + cuadrada una octava abajo (electro, industrial)
  { "LIMPIO",      1.0f, 1.0041f,  0.0f,  0.45f, 1.50f, 2.4f, 1.2f, 0.50f, 1.00f, 1.0f, false },   // dos sierras a 7 cents, poca saturación (el de las octavas del cyberpunk)
  // 808: el seno tiene UNA parcial fuerte, se satura para que tenga 3.º y 5.º armónico audibles en un
  // parlante chico; una sierra suave le da definición. Largo, y con glide.
  { "808",         0.15f, 1.0f,    0.0f,  1.0f,  3.00f, 0.4f, 0.6f, 0.90f, 1.15f, 3.5f, false },
  { "ACIDO",       1.0f, 1.0f,     0.0f,  0.0f,  1.90f, 4.5f, 1.9f, 0.30f, 0.85f, 1.0f, false },   // una sierra, resonancia y envolvente a la 303
  { "PULSO",       1.0f, 1.0f,     0.40f, 0.0f,  2.20f, 2.8f, 1.3f, 0.45f, 1.00f, 1.0f, true  },   // cuadrada al frente + sub: hueco y nasal
};

// ══ INSTRUMENTOS DE LAS MELODÍAS (BTN3 del panel de melodías) ══
//   onda: 0 pulso con PWM · 1 sierra · 2 dos sierras desafinadas · 3 seno + octava + 3.er armónico
//   corte: el filtro reposa en corte·f · envOct: cuánto lo abre cada nota · fenvTau: en cuánto se cierra
//   onda: 0 sierra · 1 dos sierras (det = razón entre ellas) · 2 sync duro (la esclava sube con la envolvente)
//         3 FM (det = razón del modulador, entera: afinado) · 4 QUINTAS (sierra + sierra a la quinta justa:
//         el power chord de guitarra)
//   ruido: cuánto ruido blanco entra ANTES del filtro (lo que las hace rasposas) · drive: saturación,
//   siempre antes del filtro · banda: la salida es el pasa-banda (lo nasal y ruidoso del STAB)
#define NUM_TMEL 6
const TimbreMel TMEL[NUM_TMEL] = {
  //  nombre    onda corte envOct q     fenvTau nivel  ruido drive det      banda
  { "ACIDO",    0, 0.9f, 3.4f, 5.5f, 0.090f, 0.90f, 0.00f, 2.2f, 1.0f,    false },   // la 303 que se abre (psytrance)
  { "FM",       3, 4.0f, 1.5f, 1.4f, 0.070f, 0.85f, 0.15f, 2.0f, 2.0f,    false },   // chillido FM con ruido (psycore)
  { "STAB",     1, 1.2f, 2.4f, 5.0f, 0.060f, 1.50f, 0.30f, 2.0f, 1.0175f, true  },   // nota de ruido pasa-banda (industrial)
  { "SYNC",     2, 6.0f, 1.2f, 1.0f, 0.250f, 0.80f, 0.00f, 2.0f, 1.0f,    false },   // sync duro que rasga (electro dark)
  { "PLUCK",    1, 0.8f, 3.0f, 1.5f, 0.070f, 1.20f, 0.30f, 1.6f, 1.0058f, false },   // sierras + ruido que se cierran en 70 ms (trap)
  { "QUINTAS",  4, 13.f, 1.2f, 0.9f, 0.060f, 1.10f, 0.00f, 3.5f, 1.0f,    false },   // riff de quintas saturadas (rock industrial)
};
const float KEYS_PWM_HZ = 0.35f, KEYS_PWM_PROF = 0.20f;
const float CHORUS_MS = 7.0f, CHORUS_PROF_MS = 2.5f, CHORUS_HZ = 0.6f, CHORUS_MEZCLA = 0.55f;
const float ECO_FEEDBACK = 0.52f, ECO_MEZCLA = 0.55f;   // eco ping-pong a corchea con puntillo
const int   TONICA_KEYS = 57, KEYS_DESDE = 60;          // La3 + transp, subida de octava si queda bajo Do4
const int   TONICA_ARP  = 57;
const float NIVEL_KEYS = 0.13f, NIVEL_ARP = 0.13f;
const float MEL_DEC_MIN = 0.060f, MEL_DEC_RANGO = 25.0f;    // 60 ms … 1.5 s
const int8_t TONOS_ARP[6] = { 0, 2, 4, 7, 9, 11 };   // dígitos del arpegio, en grados sobre la fundamental del acorde

// ══ OSCILADOR (panel 5) ══ dos osciladores ±6 cents, cada uno con su SVF resonante que sigue a la
// nota y que barre un LFO LIBRE de velocidad aleatoria (cada 1.5–6 s sortea una velocidad entre
// 0.08 y 9 Hz y se desliza hacia ella). La altura es un GRADO de la escala del estilo (BTN4/BTN5):
// siempre en tono, con un portamento corto entre grados.
// GRUÑIDO = dos sierras + una FM de razón 1 cuyo índice respira con el LFO · SIERRAS = sólo las sierras ·
// FM = sólo la FM, con realimentación (rasposa) · CUADRADAS. Sin sub: la octava de abajo es del bajo. Todo
// saturado ANTES del filtro y con el compresor propio: el nivel no cambia entre timbres.
#define NUM_TOSC 4
const char *NOMBRE_TOSC[NUM_TOSC] = { "GRUNIDO", "SIERRAS", "FM", "CUADRADAS" };
const float OSC_DRIVE[NUM_TOSC] = { 2.5f, 2.0f, 1.8f, 2.2f };
const float OSC_FM_IDX = 1.6f, OSC_FM_FB = 0.7f;
// Ajuste fino de volumen por timbre DESPUÉS del compresor: si uno queda más atrás o más adelante
// que los otros en la mezcla, se corrige acá (1.0 = como lo deja el compresor).
const float NIVEL_TOSC[NUM_TOSC] = { 1.00f, 1.00f, 1.00f, 1.00f };
const int   TONICA_OSC    = 57;          // La3 + transp del estilo: el grado 0
const int   GRADO_MIN = -7, GRADO_MAX = 14;
const float NIVEL_OSC     = 0.18f;
const float OSC_DETUNE    = 1.0035f;     // ±6 cents
const float OSC_GLIDE     = 0.030f;
const float OSC_ATAQUE    = 0.010f, OSC_SUELTA = 0.30f;
const float OSC_LFO_OCT   = 3.5f;        // profundidad máxima del LFO (POT3)
const float OSC_LFO_MIN_HZ = 0.08f, OSC_LFO_MAX_HZ = 9.0f;
const float OSC_LFO_CAMBIO_MIN = 1.5f, OSC_LFO_CAMBIO_MAX = 6.0f;
const float OSC_LFO_DESLIZ = 1.2f;
const float OSC_Q_CERRADO = 4.5f, OSC_Q_ABIERTO = 1.2f;
const float OSC_ECO_FIJO  = 0.45f;       // envío al delay (fijo: el POT4 es el volumen)
// POT1 del oscilador = la nota: OSC_NOTAS grados de la escala del estilo, desde OSC_GRADO_BAJO
// (−7 = una octava bajo la tónica) hasta una octava arriba; el centro del pot es la tónica. Cada
// zona tiene histéresis para que el ruido del ADC no haga temblar la nota.
const int   OSC_GRADO_BAJO = -7, OSC_NOTAS = 15;
const float OSC_HISTERESIS = 0.15f;      // fracción de zona que hay que pasarse para cambiar de nota
// COMPRESOR propio del oscilador: el volumen no se toca, se mantiene parejo en la mezcla aunque
// el filtro, el LFO, la resonancia, el timbre o la altura cambien cuánto suena. La salida tiende a
// OSC_COMP_NIVEL y nunca pasa de él; lo muy suave sube como mucho ×1/OSC_COMP_RODILLA. Mide el
// nivel PROMEDIO (valor absoluto suavizado), no el pico: con el pico una cuadrada y un seno quedaban
// igualados en la punta pero la cuadrada se oía mucho más fuerte.
const float OSC_COMP_NIVEL = 0.08f, OSC_COMP_RODILLA = 0.35f;
// VOLUMEN del oscilador (POT4): ganancia después del compresor con el pot al máximo. Con 1.0 (el v2, sin pot)
// se oía muy despacio. Medido (RMS): con 5.0 y el POT4 a 0.75 queda ~8 dB sobre las melodías; al máximo, casi al
// nivel de toda la mezcla.
const float OSC_VOL_MAX = 5.0f;
const float OSC_COMP_ATK = 0.030f, OSC_COMP_REL = 0.250f;
const float OSC_CHORUS_MS = 12.0f, OSC_CHORUS_PROF_MS = 4.0f, OSC_CHORUS_HZ = 0.35f, OSC_CHORUS_MEZCLA = 0.70f;
const float OSC_ANCHO     = 0.80f;

// SUBIDA (BTN5 mantenido más de 2 s, en cualquier panel) y DROP (el siguiente toque del BTN5) — igual que en el v1
const int   SUBIDA_PASOS      = 256;
const float SUBIDA_OCTAVAS    = 4.0f;
const float NIVEL_SUBIDA_OSC  = 0.22f;
const float NIVEL_SUBIDA_RUIDO = 0.20f;
const float SUBIDA_DRIVE      = 2.5f;
const float NIVEL_CAJA_SUBIDA = 0.50f;
const float NIVEL_GOLPE       = 0.85f;
const float GOLPE_REVERB      = 0.55f;
const float REVERB_FB         = 0.84f;

// ══ KITS DE BATERÍA (BTN3 del panel de batería) ══
// Pistas (0..5): HAT · CAJA · PERC · CLAP · ABIERTO · TOM. Reglas que vienen de drum_poder y del v1:
// arriba de 1 kHz todo es RUIDO filtrado (pasa-banda, nunca un pasa-altos pelado); nada de parciales
// metálicos afinados (sonaban a olla); el clap es UN golpe de ruido (tres palmadas sonaban a clipeo);
// nada de ráfagas de clics; las dos parciales de una caja NO se saturan; caídas por banda (arriba de
// 1 kHz hasta 0.14 s; 250 Hz–1 kHz hasta 0.10; 100–250 hasta 0.22).
#define NUM_IND     6
#define I_HAT       0
#define I_CAJA      1
#define I_PERC      2
#define I_CLAP      3
#define I_ABIERTO   4
#define I_TOM       5
#define NUM_KITS 5
#define K_INDUSTRIAL 0
#define K_PSY        1
#define K_DURO       2
#define K_ROCK       3
#define K_TRAP       4
//                amp    f       r2     a0    a1    eDec   cuanto caidaD  nAmt  nDec   nAcc   fc0     fc1     barre  q     hp     pan   panRnd
const Kit KITS[NUM_KITS] = {
  { "INDUSTRIAL", 47.0f, 5.5f, 0.022f, 0.24f, 0.30f, 0.25f, 1500.0f, 0.30f, 2.20f, {     // el del v1, con más pegada
    { 0.30f, 8000.0f, 0,     0,     0,    0.01f, 0,     0.01f,  1.00f, 0.020f, 0.070f, 8000.0f, 8000.0f, 0,     0.55f, 0,     0.62f, 0     },   // aire comprimido
    { 0.58f, 190.0f, 1.56f, 0.40f, 0.20f, 0.045f, 0.18f, 0.035f, 1.30f, 0.090f, 0.120f, 2100.0f, 1300.0f, 0.09f, 1.2f,  150.0f, 0.45f, 0.10f },   // martillo sobre plancha
    { 0.52f, 95.0f,  0,     0.85f, 0,    0.050f, 0.55f, 0.020f, 1.00f, 0.080f, 0.110f, 2400.0f, 300.0f,  0.08f, 1.0f,  90.0f,  0.55f, 0.20f },   // pistón + escape
    { 0.36f, 1250.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.10f, 0.040f, 0.060f, 1250.0f, 1250.0f, 0,     1.3f,  0,     0.50f, 0.08f },   // clap de un golpe
    { 0.22f, 6800.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.090f, 0.120f, 6800.0f, 6800.0f, 0,     0.5f,  0,     0.38f, 0     },   // hat abierto
    { 0.45f, 118.0f, 0,     0.80f, 0,    0.160f, 0.30f, 0.030f, 0.35f, 0.012f, 0.012f, 900.0f,  900.0f,  0,     0.8f,  60.0f,  0.50f, 0.20f } } }, // tom
  { "PSY", 52.0f, 8.0f, 0.010f, 0.09f, 0.30f, 0.25f, 2600.0f, 0.25f, 2.20f, {            // bombo corto y apretado: deja lugar al bajo rodante
    { 0.30f, 9500.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.018f, 0.040f, 9500.0f, 9500.0f, 0,     0.9f,  0,     0.62f, 0     },
    { 0.45f, 210.0f, 1.72f, 0.35f, 0.18f, 0.040f, 0.10f, 0.015f, 1.20f, 0.070f, 0.100f, 2600.0f, 2200.0f, 0.05f, 0.8f,  180.0f, 0.50f, 0     },   // caja liviana
    // ZAP: una parcial que cae de ~2.3 kHz a 110 Hz en 30 ms + un soplo de ruido (el láser del psy)
    { 0.30f, 110.0f, 0,     0.70f, 0,    0.060f, 20.0f, 0.030f, 0.25f, 0.015f, 0.020f, 3000.0f, 800.0f,  0.05f, 1.0f,  80.0f,  0.50f, 0.40f },
    { 0.36f, 1300.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.10f, 0.035f, 0.050f, 1300.0f, 1300.0f, 0,     1.4f,  0,     0.50f, 0.08f },
    { 0.24f, 8000.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.070f, 0.100f, 8000.0f, 8000.0f, 0,     0.7f,  0,     0.40f, 0     },   // el abierto del contratiempo
    { 0.42f, 130.0f, 0,     0.80f, 0,    0.100f, 0.35f, 0.020f, 0.25f, 0.010f, 0.010f, 1000.0f, 1000.0f, 0,     0.8f,  60.0f,  0.50f, 0.25f } } },
  { "DURO", 48.0f, 6.2f, 0.016f, 0.16f, 0.45f, 0.30f, 2200.0f, 0.25f, 2.40f, {           // industrial agresivo: bombo duro SIN rumble (claridad)
    { 0.30f, 8500.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.022f, 0.060f, 8500.0f, 8500.0f, 0,     1.0f,  0,     0.62f, 0     },
    { 0.60f, 115.0f, 2.0f,  0.55f, 0.22f, 0.045f, 0.60f, 0.015f, 1.30f, 0.050f, 0.080f, 1700.0f, 1700.0f, 0,    0.9f,  100.0f, 0.40f, 0.15f },   // martillo: dos parciales ENTERAS que caen + ruido
    { 0.55f, 70.0f,  0,     0.90f, 0,    0.120f, 1.10f, 0.060f, 1.10f, 0.150f, 0.180f, 3000.0f, 300.0f,  0.10f, 1.2f,  60.0f,  0.60f, 0.15f },   // pistón: una parcial grave que cae + ruido que baja
    { 0.38f, 1250.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.15f, 0.050f, 0.090f, 1250.0f, 1250.0f, 0,     1.3f,  0,     0.50f, 0.08f },
    { 0.22f, 7000.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.110f, 0.140f, 7000.0f, 7000.0f, 0,     0.5f,  0,     0.38f, 0     },
    { 0.48f, 100.0f, 0,     0.80f, 0,    0.140f, 0.40f, 0.030f, 0.30f, 0.012f, 0.012f, 900.0f,  900.0f,  0,     0.8f,  60.0f,  0.50f, 0.20f } } },
  { "ROCK", 58.0f, 2.6f, 0.030f, 0.11f, 0.35f, 0.45f, 1800.0f, 0.15f, 1.40f, {           // bombo de parche con el golpe del mazo, caja grande, platillo
    { 0.28f, 8500.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.030f, 0.060f, 8500.0f, 8500.0f, 0,     0.9f,  0,     0.66f, 0     },
    { 0.62f, 190.0f, 1.72f, 0.45f, 0.22f, 0.070f, 0.10f, 0.020f, 1.35f, 0.130f, 0.170f, 2300.0f, 1900.0f, 0.10f, 0.7f,  150.0f, 0.50f, 0     },   // caja grande + bordonera
    { 0.30f, 420.0f, 0,     0.30f, 0,    0.018f, 0,     0.01f,  1.20f, 0.018f, 0.020f, 1900.0f, 1900.0f, 0,     2.0f,  200.0f, 0.45f, 0.05f },   // aro
    { 0.36f, 1200.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.10f, 0.060f, 0.090f, 1200.0f, 1200.0f, 0,     1.2f,  0,     0.50f, 0.08f },
    { 0.30f, 5800.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.300f, 0.500f, 6200.0f, 5200.0f, 0.50f, 0.6f,  0,     0.35f, 0     },   // PLATILLO (pasa-banda, nunca pasa-altos)
    { 0.55f, 110.0f, 0,     0.80f, 0,    0.180f, 0.50f, 0.030f, 0.40f, 0.006f, 0.006f, 1500.0f, 1500.0f, 0,     0.9f,  60.0f,  0.50f, 0.30f } } },
  { "TRAP", 50.0f, 4.8f, 0.020f, 0.16f, 0.40f, 0.20f, 2600.0f, 0.20f, 2.00f, {           // el 808 lo pone el bajo: el bombo es seco
    { 0.30f, 9000.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.020f, 0.045f, 9000.0f, 9000.0f, 0,     1.1f,  0,     0.60f, 0     },
    { 0.55f, 210.0f, 1.72f, 0.40f, 0.20f, 0.050f, 0.12f, 0.015f, 1.30f, 0.100f, 0.130f, 2400.0f, 2000.0f, 0.06f, 0.8f,  160.0f, 0.50f, 0     },
    { 0.30f, 420.0f, 0,     0.30f, 0,    0.018f, 0,     0.01f,  1.20f, 0.018f, 0.020f, 1900.0f, 1900.0f, 0,     2.0f,  200.0f, 0.45f, 0.05f },   // aro
    { 0.40f, 1250.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.20f, 0.080f, 0.120f, 1250.0f, 1250.0f, 0,     1.3f,  0,     0.50f, 0.10f },
    { 0.22f, 7500.0f, 0,    0,     0,    0.01f, 0,     0.01f,  1.00f, 0.120f, 0.140f, 7500.0f, 7500.0f, 0,     0.6f,  0,     0.38f, 0     },
    { 0.50f, 95.0f,  0,     0.85f, 0,    0.200f, 0.45f, 0.040f, 0.20f, 0.008f, 0.008f, 900.0f,  900.0f,  0,     0.8f,  60.0f,  0.50f, 0.30f } } },
};

// ══ ESTILOS (BTN2 del General) ══════════════════════════════════════════════════════════════════
// BATERÍA — 7 líneas de 16 (un compás): BOMBO · HAT · CAJA · PERC · CLAP · ABIERTO · TOM
//   X golpe fuerte · x golpe normal · . golpe fantasma · r redoble de 2 en el paso · t redoble de 3 · - nada
//   En las 6 pistas la letra es también la PRIORIDAD: con la DENSIDAD (POT4 del panel) baja, sólo
//   suenan las X; al subir entran las x (y los redobles) y arriba del todo los fantasmas. El bombo no
//   depende de la densidad.
// BAJO — 16 caracteres relativos a la FUNDAMENTAL DEL ACORDE del compás, en grados de la escala:
//   x fundamental · 2 segunda · 3 tercera · 4 cuarta · 5 quinta · 6 sexta · b séptima MENOR (si en ese
//   acorde es mayor, la quinta) · o octava · - silencio. El patrón A suena tres compases y el B en el cuarto.
// MELODÍAS — 32 caracteres (2 compases), grados contados desde la fundamental del acorde: 0 el de abajo
//   (la séptima por debajo) · 1 fundamental · 3 tercera · 5 quinta · 8 octava; los pares son las notas de
//   paso que le dan el color a cada modo (en el frigio, el 2 es la b2) · - silencio. Monofónico.
//   arpegio: 16 caracteres, 0..5 = nota del acorde (0 fund · 1 3.ª · 2 5.ª · 3 8.ª · 4 10.ª · 5 12.ª), - silencio.
// FILL — 7 líneas de 4: los pasos 13–16 del compás. REEMPLAZA a la batería (nunca se suma: dos golpes
//   del mismo tambor en la misma muestra se matan) y el "1" siguiente trae un plato.
// La progresión: cada acorde es un grado de la escala con su largo en compases, apilado en terceras de la
// escala TAL CUAL.
// PATRÓN 1 de cada instrumento = el de los ejemplos que se eligieron; los otros tres van en esa dirección.
#define NUM_ESTILOS 8
#define NUM_PAT 4
const Estilo ESTILOS[NUM_ESTILOS] = {
  // ── INDUSTRIAL ── Mi eólico, i·6 VI·2 (Em … C), como en el v2. Bombo a negras, el bajo vive ENTRE los
  //    bombos (nunca cae en una negra), stab de ruido con la sexta que cae a la quinta (Do → Si).
  { "INDUSTRIAL", 0, -5, M_EOLICO, { { 0, 6 }, { 5, 2 } }, K_INDUSTRIAL, TB_SUCIO, 2, 0, 0,
    { { "-xx--o-x-xx--5-x", "-xx--o-x-xx--o5b" },     // entre los bombos (el del ejemplo)
      { "-x-x-x-x-x-x-x-x", "-x-x-x-x-x-o-x-5" },     // contratiempo en semicorcheas
      { "--xo--xo--xo--xo", "--xo--xo--xo-5o-" },     // octavas que empujan
      { "-xxx-xxx-xxx-xxx", "-xxx-xxx-xxo-x5b" } },   // rodante
    { { "X---X---X---X---", "xX.xxX.xxX.xxX.x", "--X---x---X--x-x", "-------X------x-", "----X-------X---", "--x---x---x---x-", "-----------x--xx" },   // fábrica (el del ejemplo)
      { "X---X---X---X--x", "x.Xxx.Xxx.Xxx.Xx", "---x--x---x--x--", "-------x------xx", "----X-------X---", "--X---X---X---X-", "----------------" },   // 3+3+2
      { "X-----X---X-----", "xxXxxxXxxxXxxxXx", "----X-------X---", "--x--x----x--x--", "------------X---", "------x-------x-", "-------------xxX" },   // quiebre
      { "X--xX---X--xX---", ".xXx.xXx.xXx.xXx", "--x---x---x---x-", "x-----x-x-----x-", "----X-------X--x", "--x---x---x---x-", "-----------.--x-" } },  // galope
    { "X---", "----", "xxXX", "----", "----", "----", "x.xX" },
    { "1--1--3-1-65--1-1--1--3-1-65--8-",   // el del ejemplo: la sexta que cae a la quinta
      "1-1-6-5-1-1-6-5-1-1-6-5-8-7-6-5-",   // pulso insistente
      "1---1---2-1---1-1---1---6-5---3-",   // martillo con aire
      "1-15-15-1-16-15-1-15-15-1-13-12-" }, // semicorcheas hipnóticas
    { "0-2-1-2-0-2-1-2-", "0213021302130213", "0-0-3-0-0-2-0-1-", "3210321032103210" } },

  // ── PSYTRANCE ── Mi frigio, i·7 II·1 (Em … F). Bombo-bajo rodando (bombo · bajo · bajo · bajo), el
  //    ácido sobre la tónica con la b2, hat abierto en el "y", zaps al final de la frase.
  { "PSYTRANCE", 192, -5, M_FRIGIO, { { 0, 7 }, { 1, 1 } }, K_PSY, TB_RODANTE, 0, 0, -1,
    { { "-xxx-xxx-xxx-xxx", "-xxx-xxx-xxx-222" },     // rodante (el del ejemplo)
      { "-xx--xx--xx--xx-", "-xx--xx--xx--x2-" },     // galope
      { "-x-x-x-x-x-x-x-x", "-x-x-x-x-x-x-2-o" },     // semicorcheas sueltas
      { "-xxo-xxo-xxo-xxo", "-xxo-xxo-xx2-x2o" } },   // con la octava
    { { "X---X---X---X---", ".x...x...x...x..", "----------------", "--------------x-", "----x-------x---", "--x---x---x---x-", "----------------" },   // el del ejemplo
      { "X---X---X---X---", "xxXxxxXxxxXxxxXx", "------------x---", "---x-------x----", "----X-------X---", "--X---X---X---X-", "----------------" },
      { "X---X---X---X---", ".x.x.x.x.x.x.x.x", "----x-------x--x", "-------x-------x", "----x-------x---", "--x---x---x---x-", "-------------x.x" },
      { "X---X---X---X---", "x.xXx.xXx.xXx.xX", "--------x-------", "x-----------x---", "----X-------X---", "--x---x---x---x-", "----------------" } },
    { "X---", "xxxx", "x-xX", "--x-", "----", "----", "----" },
    { "1-121-315-10211-1-121-31--1022--",   // el ácido del ejemplo
      "1--1--1-2--1--1-1--1--1-2--1-3-2",   // una nota y la b2
      "11-1-11-1-21-1-311-1-11-1-21-5-8",   // semicorcheas que tartamudean
      "1---2---1---3---1---2---1---0---" }, // suspiros lentos
    { "0-0-1-0-0-0-2-0-", "0101010101010101", "0-12-0-12-0-12-0", "3-2-1-0-3-2-1-0-" } },

  // ── PSYCORE ── Fa# doble armónica (F# G A# B C# D E#), I·4 bII·2 I·2 (F# … G … F#). El bajo rueda
  //    entre la tónica y la b2; la FM chilla con las segundas aumentadas; redobles en los hats.
  { "PSYCORE", 224, -3, M_DOBLE, { { 0, 4 }, { 1, 2 }, { 0, 2 } }, K_PSY, TB_RODANTE, 1, 2, 0,
    { { "-xxx-xx2-xxx-xx2", "-xxx-xx2-x5x-2x5" },     // el del ejemplo
      { "-x2x-x2x-x2x-x2x", "-x2x-x2x-x5x-525" },
      { "-xx2-xx2-xx2-xx2", "-xx2-xx2-x55-x2o" },
      { "-xxx-xxx-xxx-xxx", "-xxx-xxx-5xx-2xx" } },
    { { "X---X---X---X---", "x.xXx.xXx.xXx.xr", "--------x-------", "------------x---", "----x-------x---", "--x---x---x---x-", "----------------" },   // el del ejemplo
      { "X---X---X---X---", "xxXxxxXxxxXxxxXr", "--------x-----.r", "---x------x-----", "----X-------X---", "--x---x---x---x-", "--------------x." },
      { "X---X---X---X--x", ".xXx.xXx.xXx.xXt", "----x--x----x---", "-x-------x------", "------------X---", "--X---X---X---X-", "-----------x-x-x" },
      { "X---X---X---X---", "x.x.x.x.x.x.xrxr", "--------X-------", "x---x-------x---", "----X-------X---", "--x---x---x---x-", "----------------" } },
    { "X-x-", "rrrr", "xxXX", "x-x-", "----", "----", "----" },
    { "115-2---115-6-5-1--222--115-8-5-",   // el del ejemplo: tónica, tritono, b2
      "1-5-1-5-1-5-2-5-1-5-1-5-1-5-8-5-",
      "1111----5555----2222----1111----",
      "8-5-2-1-8-5-2-1-8-5-2-1-5-2-1---" },
    { "0-1-2-1-0-1-2-1-", "0-0-2-0-0-0-2-0-", "0213021302130213", "2-1-0-1-2-1-0-1-" } },

  // ── ELECTRO DARK ── Fa frigio, i·2 II·1 i·1 (F … Gb … F): la tónica que se queda y sube un semitono.
  //    Bajo sucio sincopado, martillo en contratiempo, el sync que rasga con motivos de semitono.
  { "ELECTRO DARK", 160, -4, M_FRIGIO, { { 0, 2 }, { 1, 1 }, { 0, 1 } }, K_INDUSTRIAL, TB_SUCIO, 3, 0, 0,
    { { "x-xo-x-xx-x-2-ox", "x-xo-x-xx-x-2-o5" },     // el del ejemplo
      { "x-x-x-x-x-x-x-x-", "x-x-x-x-x-x-o-2-" },
      { "x--x--x-x--x--x-", "x--x--x-x--x-o2-" },
      { "-xx--xx--xx--xx-", "-xx--xx--xx--o5-" } },
    { { "X---X---X---X---", "--x---x---x---x.", "----X-------X---", "---x--x----x--x-", "----X-------X---", "----------------", "----------------" },   // el del ejemplo
      { "X---X---X---X---", "x-x-x-x-x-x-x-x-", "----X-------X---", "---x--x----x-xx-", "----X-------X---", "--x---x---x---x-", "----------------" },
      { "X-----x-X---X---", ".x.x.x.x.x.x.x.x", "----X-------X--.", "-x--x--x--x--x--", "----x-------x---", "------x-------x-", "-------------x.x" },
      { "X---X---X---X-x-", "--x---x---x---xr", "----X--.----X---", "x--x--x---x--x--", "----X-------X---", "--------------x-", "----------------" } },
    { "X---", "x.x.", "-xxX", "x-x-", "----", "----", "--xx" },
    { "5--65---3-3-1---5--67---6-5-3---",   // el del ejemplo: Do Re♭ Do · La♭ · Fa
      "1-2-1-----5-6-5-1-2-1-----2-1-0-",
      "5---6---5---3---5---6---8---7---",
      "1--1--2-1--1--2-1--1--2-5--3--2-" },
    { "0-1-2-1-0-1-2-1-", "0-0-0-0-2-2-1-1-", "0213021302130213", "00-100-200-100-3" } },

  // ── INDUSTRIAL DURO ── Re frigio dominante (hijaz: D Eb F# G A Bb C), un solo acorde (D). Martillos y
  //    pistones en 3+3+2, una nota de ruido pasa-banda que insiste con la b2. Hipnótico.
  { "INDUSTRIAL DURO", 16, -7, M_FRIGDOM, { { 0, 8 } }, K_DURO, TB_SUCIO, 2, 0, -1,
    { { "--x---x---x---x-", "--x---x---x---2-" },
      { "-x-x-x-x-x-x-x-x", "-x-x-x-x-x-x-2-5" },
      { "-xx--xx--xx--xx-", "-xx--xx--xx--25-" },
      { "--xx--xx--xx--xx", "--xx--xx--x2--55" } },
    { { "X---X---X---X---", "x.X.x.X.x.X.x.X.", "X--x--x---X--x--", "-------x-------x", "----x-------x---", "--x---x---x---x-", "----------------" },   // el del ejemplo
      { "X---X---X---X---", "xxXxxxXxxxXxxxXx", "X--x--x---X-xx-x", "-------x-----x-x", "----X-------X---", "--x---x---x---x-", "----------------" },
      { "X---X---X--xX---", ".x.x.x.x.x.x.x.x", "--x--x--x--x--x-", "x-------x-------", "------------X---", "--X---X---X---X-", "-------------x.x" },
      { "X--xX---X--xX---", "x.xXx.xXx.xXx.xX", "----X-------X---", "--x---x---x---x-", "----X-------X---", "----------------", "---------x--x--x" } },
    { "X-xX", "----", "xxXX", "x-x-", "----", "----", "x.x-" },
    { "1--1--2-1-1--25-1--1--2-1-1--25-",   // el del ejemplo: Re · Mi♭ · La♭
      "1-1-1-2-1-1-1-5-1-1-1-2-1-1-2-2-",
      "1---1---2---1---5---5---2---1---",
      "1--21--21--21--51--21--21--2512-" },
    { "0-0-1-0-0-0-2-0-", "0-1-0-2-0-1-0-2-", "0000200001000200", "2-1-0-1-2-1-0-1-" } },

  // ── CYBERPUNK ── Do# dórico, i·1 bVII·1 IV·1 v·1 (C#m B F# G#m: el IV mayor es el color del dórico).
  //    Caja a medio tiempo, bajo de octavas en semicorcheas (sierra limpia), arpegio sucio.
  { "CYBERPUNK", 200, -8, M_DORICO, { { 0, 1 }, { 6, 1 }, { 3, 1 }, { 4, 1 } }, K_ROCK, TB_LIMPIO, 4, 0, 0,
    { { "xoxoxoxoxoxoxoxo", "xoxoxoxoxoxoxo5o" },     // el del ejemplo
      { "x-o-x-o-x-o-x-o-", "x-o-x-o-x-o-5-o-" },
      { "xxoxxxoxxxoxxxox", "xxoxxxoxxxoxx5o5" },
      { "x--xo--xx--xo-x-", "x--xo--xx--xo55-" } },
    { { "X------X--X-----", "x-x.x-x.x-x.x-xr", "--------X-------", "----------------", "----------------", "----------------", "----------------" },   // el del ejemplo
      { "X------X--X---x-", "x-x.x-x.x-x.x-x.", "--------X-----.-", "---.-------.----", "--------x-------", "------x-------x-", "----------------" },
      { "X---X---X---X---", "x-x-x-x-x-x-x-x-", "----X-------X---", "--------------x-", "----X-------X---", "--x---x---x---x-", "----------------" },
      { "X-x-----X-x-----", ".x.x.x.x.x.x.x.x", "----X-------X---", "-------.-------.", "----------------", "--------------x-", "-----------x-xx-" } },
    { "X---", "----", "x.xX", "----", "----", "----", "xxXX" },
    { "5-----3-------1-8-----7-5-------",
      "1---3---5---8---7---5---3---0---",
      "8-------7-------5-------3-------",
      "1-3-5-3-1-3-5-3-1-3-5-3-8-7-5-3-" },
    { "0212021302120213", "0-1-2-3-2-1-0-1-", "0303030303030303", "3210321032103210" } },   // el 1.º = la figura del ejemplo

  // ── TRAP ── Fa dórico, i·1 bIII·1 bVII·1 IV·1 (Fm Ab Eb Bb) a medio tiempo. 808 con glide, caja en el 3,
  //    hats con redobles, el pluck oscuro con mucho aire (el 6d: pregunta y respuesta).
  { "TRAP", 176, -4, M_DORICO, { { 0, 1 }, { 2, 1 }, { 6, 1 }, { 3, 1 } }, K_TRAP, TB_808, 4, 1, 0,
    { { "x-----x---o---x-", "x-----x---o---5-" },     // el del ejemplo
      { "x-----x-x-----x-", "x-----x-x---o-5-" },
      { "x---------x-----", "x---------x---o-" },
      { "x--x--x---x-x---", "x--x--x---o-5-x-" } },
    { { "X-----x---x-----", "x-x-x-xrx-x-x-tx", "--------X-------", "----------------", "--------X-------", "----------------", "----------------" },   // el del ejemplo
      { "X-----x-x-----x-", "x-x-xrx-x-x-xrx-", "--------X-----.-", "---.------.-----", "--------X-------", "--------------x-", "----------------" },
      { "X-x---x---x-x---", "xrx-xrx-xtx-xrx-", "--------X-------", "------x-------x-", "--------X-------", "----------------", "-------------xxx" },
      { "X-----x-x-x---x-", "x-xtxrx-x-xrx-xt", "--------X-----..", "-.-------.------", "--------X-------", "------x---------", "----------------" } },
    { "X---", "tttt", "--xX", "----", "--x-", "----", "xxXX" },
    { "5--6--5---------0--1--3-1-------",   // el 6d: la llamada (Do Re♭ Do) y la respuesta
      "8------5--------8------5---3----",
      "1--3--5---------8-------7-5-----",
      "5-----6-5-------3-----2-1-------" },
    { "----------------", "0-------2-------", "----0-------2---", "0---2---1---3---" } },

  // ── ROCK INDUSTRIAL ── Mi frigio dominante (E F G# A B C D), I·6 bII·2 (E … F). Batería de rock (bombo 1 · "y" del 2 · 3, caja
  //    2 y 4, hats en corcheas, platillo en el 1), el riff de quintas saturadas en las melodías (dos
  //    octavas abajo) y el bajo que lo dobla.
  { "ROCK INDUSTRIAL", 8, -5, M_FRIGDOM, { { 0, 6 }, { 1, 2 } }, K_ROCK, TB_SUCIO, 5, 1, -2,
    { { "xxx2-xx3xx54-x32", "xxx2-xx3xx54----" },     // dobla el riff del ejemplo
      { "x-x-x-x-x-x-x-x-", "x-x-x-x-x-x-5-4-" },
      { "x--x--x-x--x--x-", "x--x--x-x--x-2--" },
      { "xxxxxxxxxxxxxxxx", "xxxxxxxx2222xxxx" } },
    { { "X-----x-X-x-----", "X-x-X-x-X-x-X-x-", "----X-------X---", "----------------", "----------------", "----------------", "----------------" },   // el del ejemplo
      { "X-----x-X-x---x-", "X-x-X-x-X-x-X-x-", "----X-------X--.", "-------.--------", "----------------", "----------------", "----------------" },
      { "XxX-x-x-XxX-x-x-", "----------------", "----X-------X---", "----------------", "----------------", "x-x-x-x-x-x-x-x-", "----------------" },   // doble bombo con el platillo en corcheas
      { "X--X--X-X--X--X-", "x-x-x-x-x-x-x-x-", "--------X-------", "----------------", "----------------", "X---------------", "------------xxXX" } },   // medio tiempo pesado
    { "X---", "----", "-x-X", "----", "----", "----", "XxX-" },
    { "1112-1131154-1321112-1131154----",   // el riff del ejemplo: Mi Mi Mi Fa · Sol · Si♭ La
      "1-1-1-2-1-1-3-2-1-1-1-2-1-5-4-2-",
      "1--1--1-1--1--2-1--1--1-1--5-4--",
      "1-11-11-1-11-11-1-11-11-1-11-2-3" },
    { "0-------2-------", "----------------", "0---2---3---2---", "----------------" } },
};

// Eventos (para los LEDs, la matriz y las proyecciones)
#define EV_KEYS     6
#define EV_BOMBO    7
#define EV_BAJO     8
#define EV_ARP      9
#define EV_OSC     10
#define EV_MUESTRA 11
#define NUM_EV     12

// Paneles
#define P_GENERAL   0
#define P_BAJO      1
#define P_DRUM      2
#define P_MEL       3
#define P_OSC       4
#define P_SAMPLES   5
#define NUM_PANELES 6
const char *NOMBRE_PANEL[NUM_PANELES] = { "GENERAL", "BAJO", "BATERIA", "MELODIAS", "OSCILADOR", "SAMPLES" };

// ==============================================================================================================================================
// PEDIDOS ENTRE NÚCLEOS
// ==============================================================================================================================================
// control → audio
volatile Ctl      reqC;                            // se copia entera (memcpy) al borde de cada buffer
volatile uint32_t reqFill = 0, reqFillNo = 0, reqSubida = 0, reqDrop = 0;
volatile uint32_t reqBanco[NUM_BANCOS] = { 0, 0, 0 };   // un contador por botón del panel Samples
// Pots de cada panel: la tarea de control los escribe ya suavizados; el audio los lee.
//   [P_GENERAL] vol · tempo · swing · pasa-bajos     [P_BAJO] vol · filtro · ataque · caída
//   [P_DRUM]    vol · filtro · largo · densidad      [P_MEL]  vol · filtro · ataque · caída
//   [P_OSC]     nota · filtro · LFO · volumen      [P_SAMPLES] vol · pitch · reverb · eco
volatile float    pPot[NUM_PANELES][4] = {
  { 0.70f, 0.58f, 0.00f, 1.00f },
  { 0.88f, 0.35f, 0.35f, 0.408f },                // arrancan en el bajo de siempre (2 ms · 0.16 s)
  { 0.88f, 1.00f, 0.50f, 0.60f },
  { 0.88f, 0.50f, 0.00f, 0.43f },
  { 0.50f, 0.30f, 0.60f, 0.75f },                // POT1 = nota: al centro, la tónica · POT4 = volumen
  { 0.80f, 0.50f, 0.25f, 0.20f },                // samples: pitch al centro = como está
};

// audio → LEDs y USB
volatile uint32_t golpes[NUM_EV];
volatile float    velGolpe[NUM_EV];
volatile uint32_t pasoTotalPub = 0, contadorPasos = 0;
volatile float    envBajoPub = 0.0f, bpmPub = 128.0f, fasePub = 0.0f;
volatile float    nivelPub[5];                     // pico por bloque: batería · bajo · melodías · oscilador · master
volatile int      notaBajoPub = 0, notaKeysPub = 64, notaArpPub = 64, notaOscPub = -1, acordePub = 0, tonicaPub = 57;
volatile bool     tocandoPub = false, breakPub = false, fillPub = false, repeatPub = false;
volatile int      estadoSubPub = 0;
volatile uint32_t muestraPub = 0;
volatile uint32_t cargaAltaHasta = 0;
volatile uint32_t medMax = 0, medSuma = 0, medN = 0, medTecho = 0;   // MEDIR
volatile float    medLimMin = 1.0f, medPico = 0.0f;
volatile int      panelPub = 0;                    // lo escribe el control, lo leen los LEDs

// ==============================================================================================================================================
// DSP — utilidades (sin libm por muestra)
// ==============================================================================================================================================
#define TABLA 1024
float SENO[TABLA + 1];
static inline float seno(float fase) { float x = fase * TABLA; int i = (int)x; float f = x - i; return SENO[i] + (SENO[i + 1] - SENO[i]) * f; }
uint32_t semillaRuido = 22222;
static inline float ruido(uint32_t &s) { s = s * 1664525u + 1013904223u; return (int32_t)s * (1.0f / 2147483648.0f); }
static inline float tanRapido(float x) { float x2 = x * x; return x * (15.0f - x2) / (15.0f - 6.0f * x2); }   // Padé, < 0.2 % hasta 13 kHz
static inline float exp2Rapido(float x) {          // 2^x para x ≥ 0, error < 0.2 %
  int e = (int)x; float f = x - e;
  float p = 1.0f + f * (0.6951786f + f * (0.2261487f + f * 0.0782451f));
  union { float f; int32_t i; } u; u.f = p; u.i += e << 23; return u.f;
}
static inline float decaimiento(float tau) { return expf(-1.0f / (tau * SR)); }
static inline float softClip(float x) {
  if (x > 3.0f) return 1.0f; if (x < -3.0f) return -1.0f;
  float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// Saturación cúbica SIN división (softClip tiene una: en el S3 una división de float cuesta como diez
// multiplicaciones). Lineal al centro, llega a ±1 con pendiente cero en ±1.5.
static inline float sat3(float x) { if (x > 1.5f) return 1.0f; if (x < -1.5f) return -1.0f; return x - 0.148148f * x * x * x; }
static inline float vuelta(float p) { return p < 0.0f ? p + 1.0f : (p >= 1.0f ? p - 1.0f : p); }   // fase a [0,1) cuando se pasa por menos de una vuelta
static inline float sierra(float &f, float inc, float inv) {   // PolyBLEP; inv = 1/inc
  float y = 2.0f * f - 1.0f;
  if (f < inc) { float t = f * inv; y -= t + t - t * t - 1.0f; }
  else if (f > 1.0f - inc) { float t = (f - 1.0f) * inv; y -= t * t + t + t + 1.0f; }
  f += inc; if (f >= 1.0f) f -= 1.0f;
  return y;
}
static inline float pulso(float &f, float inc, float inv, float w) {   // PolyBLEP de ancho w: sierra − sierra desplazada
  float p2 = f + w; if (p2 >= 1.0f) p2 -= 1.0f;
  float a = 2.0f * f - 1.0f, b = 2.0f * p2 - 1.0f;
  if (f < inc) { float t = f * inv; a -= t + t - t * t - 1.0f; } else if (f > 1.0f - inc) { float t = (f - 1.0f) * inv; a -= t * t + t + t + 1.0f; }
  if (p2 < inc) { float t = p2 * inv; b -= t + t - t * t - 1.0f; } else if (p2 > 1.0f - inc) { float t = (p2 - 1.0f) * inv; b -= t * t + t + t + 1.0f; }
  f += inc; if (f >= 1.0f) f -= 1.0f;
  return (a - b) * 0.5f;
}
static inline float cuadrada(float &f, float inc, float inv) { return pulso(f, inc, inv, 0.5f); }
static inline float suave(float &f, float inc) {                // seno + octava: casi un seno, con algo de cuerpo
  f += inc; if (f >= 1.0f) f -= 1.0f;
  float p2 = f + f; if (p2 >= 1.0f) p2 -= 1.0f;
  return (seno(f) + 0.25f * seno(p2)) * 0.8f;
}
static inline void coefFiltro(Filtro &s, float fc, float q) {
  if (fc > 12500.0f) fc = 12500.0f; if (fc < 20.0f) fc = 20.0f;
  float g = tanRapido(PI_F * fc / SR); s.k = 1.0f / q;
  s.a1 = 1.0f / (1.0f + g * (g + s.k)); s.a2 = g * s.a1; s.a3 = g * s.a2;
}
static inline void pasoFiltro(Filtro &s, float x, float &lp, float &bp) {
  float v3 = x - s.ic2, v1 = s.a1 * s.ic1 + s.a2 * v3, v2 = s.ic2 + s.a2 * s.ic1 + s.a3 * v3;
  s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
  lp = v2; bp = v1 * s.k;                          // pasa-banda con ganancia 1 en el centro
}
static inline float pasoBQ(BQ &q, float x) { float y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
void bqBPF(BQ &c, float fc, float Q) {
  float w = 2.0f * PI_F * fc / SR, s = sinf(w), co = cosf(w), al = s / (2.0f * Q), a0 = 1.0f + al;
  c.b0 = al / a0; c.b1 = 0.0f; c.b2 = -al / a0; c.a1 = -2.0f * co / a0; c.a2 = (1.0f - al) / a0; c.z1 = c.z2 = 0.0f;
}
void bqHPF(BQ &c, float fc, float Q) {
  float w = 2.0f * PI_F * fc / SR, s = sinf(w), co = cosf(w), al = s / (2.0f * Q), a0 = 1.0f + al;
  c.b0 = (1.0f + co) * 0.5f / a0; c.b1 = -(1.0f + co) / a0; c.b2 = c.b0; c.a1 = -2.0f * co / a0; c.a2 = (1.0f - al) / a0; c.z1 = c.z2 = 0.0f;
}
void bqLPF(BQ &c, float fc, float Q) {
  float w = 2.0f * PI_F * fc / SR, s = sinf(w), co = cosf(w), al = s / (2.0f * Q), a0 = 1.0f + al;
  c.b0 = (1.0f - co) * 0.5f / a0; c.b1 = (1.0f - co) / a0; c.b2 = c.b0; c.a1 = -2.0f * co / a0; c.a2 = (1.0f - al) / a0; c.z1 = c.z2 = 0.0f;
}
static inline float techoSuave(float x) {
  float a = fabsf(x); if (a <= 0.95f) return x;
  float y = 0.95f + 0.05f * tanhf((a - 0.95f) / 0.05f); return x < 0 ? -y : y;
}
static inline float hz(float midi) { return 440.0f * exp2f((midi - 69.0f) / 12.0f); }
static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
// Pot de volumen → ganancia: cuadrática, 1.3 arriba del todo (≈ +2 dB sobre el nivel del v1, que
// cae en el 88 %). Con 1.56 y los cuatro instrumentos al máximo el limitador bajaba 9 dB (simulado).
static inline float ganVol(float v) { return 1.3f * v * v; }
// Corte de un pasa-bajos de bus (batería y general): 150 Hz … 19 kHz; arriba del 92 % se abre del todo
static inline float corteBus(float p) { return 150.0f * exp2Rapido(7.0f * clamp01(p)); }
static inline float abiertoBus(float p) { return clamp01((p - 0.90f) / 0.08f); }

// IMA ADPCM (el de sinfonia_natural): bloques de 1024 muestras, cada uno con su cabecera (predictor + índice),
// así se puede empezar a leer en cualquier bloque. Las mismas tablas usa preparar_samples.py.
#define ADPCM_BLOQUE 1024
#define ADPCM_BYTES  (3 + ADPCM_BLOQUE / 2)
const int16_t PASO_IMA[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
  118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
  27086, 29794, 32767 };
const int8_t INDICE_IMA[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
void adpcmIniciar(Adpcm &d, const uint8_t *datos, uint32_t bloque) { d.p = datos + bloque * ADPCM_BYTES; d.n = 0; d.pred = 0; d.idx = 0; }
static inline int adpcmSig(Adpcm &d) {
  if ((d.n & (ADPCM_BLOQUE - 1)) == 0) {           // bloque nuevo: el estado viene en su cabecera
    d.pred = (int16_t)(d.p[0] | (d.p[1] << 8)); d.idx = d.p[2]; d.p += 3;
  }
  int c = (d.n & 1) ? (*d.p++ >> 4) : (*d.p & 15);
  int paso = PASO_IMA[d.idx], dif = paso >> 3;
  if (c & 4) dif += paso;
  if (c & 2) dif += paso >> 1;
  if (c & 1) dif += paso >> 2;
  d.pred += (c & 8) ? -dif : dif;
  if (d.pred > 32767) d.pred = 32767; else if (d.pred < -32768) d.pred = -32768;
  d.idx += INDICE_IMA[c]; if (d.idx < 0) d.idx = 0; else if (d.idx > 88) d.idx = 88;
  d.n++;
  return d.pred;
}
int16_t MULAW[256];                                 // G.711 μ-law → 16 bit (se llena en prepararSintesis)
void armarMulaw() {
  for (int i = 0; i < 256; i++) {
    int b = ~i & 0xFF, e = (b >> 4) & 7, m = b & 0x0F, v = (((m << 3) + 132) << e) - 132;
    MULAW[i] = (int16_t)((b & 0x80) ? -v : v);
  }
}

// Generador de composición — separado del ruido de audio
uint32_t rngComp = 0x9E3779B9u;
static inline float frnd() { rngComp ^= rngComp << 13; rngComp ^= rngComp >> 17; rngComp ^= rngComp << 5; return (rngComp >> 8) * (1.0f / 16777216.0f); }

// ==============================================================================================================================================
// ESTADO DE LA SÍNTESIS (lo toca sólo la tarea de audio)
// ==============================================================================================================================================
Ctl C;                                              // la copia de los controles de este bloque
int estilo = 0, kit = 0, tBajo = 0, tMel = 0, tOsc = 0;
VozBombo bombos[2]; int sigBombo = 0;              // dos instancias: la que sonaba se calla en 3 ms
struct { float f1, f2, fs, fsn, inc1, incObj, env, envF; bool atk, atkF; Filtro s; } bajo;
float dBajo, dBajoF, dRapido3, dRapido, dSc, kGlideBajo, kGlide16;
float incAtkBajo = 1.0f / 88.0f, incAtkMel = 1.0f / 132.0f, decMel = 0.20f, largoDrum = 1.0f;
float filtMel = 1.0f;
uint32_t semRuidoL = 12345u, semRuidoR = 67891u;
// Subida (como en el v1)
#define E_NORMAL 0
#define E_SUBIDA 1
#define E_ESPERA 2
int      estadoSub = E_NORMAL, subPasos = 0;
uint32_t vistoSubida = 0, vistoDrop = 0;
float    subF1 = 0.0f, subF2 = 0.31f, subLp = 0.0f, subTrem = 1.0f, faltanTramo = 0.0f, corteGate = 1.0f;
struct { float env, eC, nz, nC, ph1, ph2, amp; Filtro s; } cajaSub;
struct { bool viva; float f, ph, env, eC, nz, nC; Filtro s; } golpeFin;
#define RV_N1 1116
#define RV_N2 1188
#define RV_N3 1277
#define RV_N4 1356
#define RV_A1 556
#define RV_A2 441
#define RV_A3 579
#define RV_A4 467
float rvC1[RV_N1], rvC2[RV_N2], rvC3[RV_N3], rvC4[RV_N4], rvA1[RV_A1], rvA2[RV_A2], rvA3[RV_A3], rvA4[RV_A4];
int   rvI1 = 0, rvI2 = 0, rvI3 = 0, rvI4 = 0, rvJ1 = 0, rvJ2 = 0, rvJ3 = 0, rvJ4 = 0;
float rvLp1 = 0, rvLp2 = 0, rvLp3 = 0, rvLp4 = 0;
int   reverbViva = 0;
float golpeL[BUFFER_SAMPLES], golpeR[BUFFER_SAMPLES];
// Oscilador
struct { bool vivo; float f1, f2, inc, incObj, amp, obj, comp, m1, y1; Filtro s, s2; } osc;
float lfoFase = 0.0f, lfoVel = 2.0f, lfoVelObj = 2.0f, lfoFalta = 0.0f; uint32_t semLfo = 0x51F15EEDu;
#define OSC_CHORUS_N 2048
float oscChorusBuf[OSC_CHORUS_N]; int oscChorusIdx = 0; float oscChorusLfo = 0.0f;
const float octLfo = log2f(OSC_LFO_MAX_HZ / OSC_LFO_MIN_HZ);
const float kDeslizLfo = 1.0f - expf(-16.0f / (OSC_LFO_DESLIZ * SR));
float dOscGlide, kOscAtk, kOscSuelta;
float oscFiltS = 0.3f, oscLfoS = 0.6f, oscVolS = 0.75f;
int   notaOscVista = -999, gradoPot = 7; bool oscOnVisto = false;
float oscCompEnv = 0.0f, oscCompG = 1.0f, kOscCompAtk, kOscCompRel;
float oscL[BUFFER_SAMPLES], oscR[BUFFER_SAMPLES], oscEco[BUFFER_SAMPLES];
VozInd ind[NUM_IND][2]; int sigInd[NUM_IND];       // un "parche" por pista con fast-kill al retriggear
Redoble redobles[NUM_IND];                         // los golpes que faltan de un redoble (r / t) y cuándo
float largoPaso = 0.0f;                            // muestras que dura la semicorchea que acaba de empezar (con swing)
uint32_t semArp = 0x13579BDFu;
// SAMPLES
VozMuestra muestras[VOCES_MUESTRA]; int sigMuestra = 0, ultimaMuestra[NUM_BANCOS] = { -1, -1, -1 };
VozRecorte extras[NUM_EXTRA][2]; int sigExtra[NUM_EXTRA];
float qMelMul = 1.0f, compMel = 1.0f;
uint32_t vistoBanco[NUM_BANCOS] = { 0, 0, 0 };
float   pasoMuestra = 1.0f, revMuestraS = 0.25f, ecoMuestraS = 0.04f, gMuestraB = 0.8f;
float   smpL[BUFFER_SAMPLES], smpR[BUFFER_SAMPLES], smpRev[BUFFER_SAMPLES], smpEco[BUFFER_SAMPLES];
float scObj = 0, sc = 0;
float prio[NUM_IND][16];
uint8_t rangoExtra[NUM_EXTRA][16];                 // percusión extra: orden de cada hueco del patrón (99 = no es hueco)                           // prioridad de cada golpe del patrón: suena si prio < densidad

// Secuenciador
bool     aTocando = false, aBreak = false;
float    fundido = 0.0f;
float    sps = 0.0f, faltan = 0.0f;                // muestras por semicorchea · hasta el próximo paso
float    bpm = 128.0f, swingS = 0.5f;
uint32_t pasoTotal = 0xFFFFFFFFu;                  // semicorcheas desde el Play (el reloj maestro, nunca se corre)
uint32_t muestraGlobal = 0;
// Beat repeat, media velocidad y fill
bool     repAntes = false; uint32_t repAncla = 0;
bool     mitadAntes = false; int32_t mitadAncla = 0;
uint32_t vistoFill = 0, vistoFillNo = 0; bool fillPend = false, crashPend = false; uint32_t fillCompas = 0;
// Melodías
#define VOCES_ARP   2
VozPluck arps[VOCES_ARP];
struct { bool viva, atk; float f1, f2, inc1, inv1, inc2, inv2, amp, fenv, base, eC, lfo, fb; Filtro s; } kv;   // MELODÍA: monofónica
#define CHORUS_N 1024
float    chorusBuf[CHORUS_N]; int chorusIdx = 0; float chorusLfo = 0.0f;
#define ECO_MAX 32768                              // 0.74 s: corchea con puntillo hasta 60 BPM
int16_t  ecoL[ECO_MAX], ecoR[ECO_MAX];
int      ecoIdx = 0, ecoN = 15000, ecoN2 = 15000; float ecoXf = 1.0f, ecoLpL = 0, ecoLpR = 0, ecoHpL = 0, ecoHpR = 0;
float    keysBuf[BUFFER_SAMPLES], ecoBufL[BUFFER_SAMPLES], ecoBufR[BUFFER_SAMPLES];
float    dKeysFenv, dArpFenv;
int      modo = 0, transp = -5, tonicaBajo = 28, ladoArp = 0;
int      estiloPrio = -1, patPrio = -1;

// Master y buses
float bufKick[BUFFER_SAMPLES], bufBajo[BUFFER_SAMPLES], bufIndL[BUFFER_SAMPLES], bufIndR[BUFFER_SAMPLES];
float bufMelL[BUFFER_SAMPLES], bufMelR[BUFFER_SAMPLES], bufFxL[BUFFER_SAMPLES], bufFxR[BUFFER_SAMPLES], bufSc[BUFFER_SAMPLES];
float corteBajoS = 0.35f, volS = 0.5f, densS = 0.6f, lpfS = 1.0f, filtDrumS = 1.0f;
float gDrumS = 1, gKickS = 1, gBajoS = 1, gMelS = 1, gOscS = 1;
Filtro fDrumL, fDrumR, fMasL, fMasR;
float dcX1L = 0, dcY1L = 0, dcX1R = 0, dcY1R = 0;
float limDlyL[LIM_LOOK], limDlyR[LIM_LOOK]; int limDlyIdx = 0;
float limEnv = 0.0f, limGain = 1.0f, limRel = 0.0f;
BQ techoL, techoR;
float nivAcum[5];

void setBPM(float b) {
  if (b < BPM_MIN) b = BPM_MIN; if (b > BPM_MIN + BPM_RANGO) b = BPM_MIN + BPM_RANGO;
  bpm = b; sps = SR * 60.0f / bpm / 4.0f; bpmPub = b;
}

void prepararSintesis() {
  for (int i = 0; i <= TABLA; i++) SENO[i] = sinf(2.0f * PI_F * i / TABLA);
  armarMulaw();
  dRapido3 = decaimiento(0.003f); dRapido = decaimiento(0.0015f); dSc = decaimiento(0.14f);
  dBajo = decaimiento(0.16f); dBajoF = decaimiento(0.16f * 0.55f);
  dArpFenv = decaimiento(0.07f); dKeysFenv = decaimiento(0.07f);
  kGlideBajo = 1.0f - decaimiento(BAJO_GLIDE); kGlide16 = 1.0f - powf(1.0f - kGlideBajo, 16.0f);   // el glide avanza cada 16 muestras
  dOscGlide = 1.0f - decaimiento(OSC_GLIDE); kOscAtk = 1.0f - decaimiento(OSC_ATAQUE); kOscSuelta = 1.0f - decaimiento(OSC_SUELTA);
  kOscCompAtk = 1.0f - decaimiento(OSC_COMP_ATK); kOscCompRel = 1.0f - decaimiento(OSC_COMP_REL);
  for (int i = 0; i < LIM_LOOK; i++) { limDlyL[i] = 0; limDlyR[i] = 0; }
  limRel = expf(-1.0f / (SR * 0.08f));
  bqLPF(techoL, 13000.0f, 0.7071f); bqLPF(techoR, 13000.0f, 0.7071f);   // DESPUÉS del limitador
  coefFiltro(fDrumL, 12000.0f, 0.75f); fDrumR = fDrumL; fMasL = fDrumL; fMasR = fDrumL;
  setBPM(128.0f);
}

// ─── Armonía ───────────────────────────────────────────────
int notaGrado(int tonica, int g) {                  // grado de la escala (puede ser negativo) → nota MIDI
  int o = (g >= 0) ? g / 7 : -((-g + 6) / 7);
  return tonica + 12 * o + ESCALAS[modo][g - 7 * o];
}
bool triadaConsonante(int g) {                      // ¿quinta justa con tercera mayor o menor?
  int f = notaGrado(0, g), t = notaGrado(0, g + 2) - f, q = notaGrado(0, g + 4) - f;
  return q == 7 && (t == 3 || t == 4);
}
int acordeDelModo(int g) {                          // disminuido o aumentado → el consonante más cercano
  const int8_t PRUEBA[5] = { 0, -2, 2, -1, 1 };
  for (int k = 0; k < 5; k++) { int c = ((g + PRUEBA[k]) % 7 + 7) % 7; if (triadaConsonante(c)) return c; }
  return 0;
}
inline int raizCercana(int g) { return g >= 4 ? g - 7 : g; }   // los acordes altos se toman desde abajo: las voces se mueven poco
int notaAcorde(int tonica, int raiz, int off, bool cercana) {
  int g = cercana ? raizCercana(raiz) : raiz;
  return notaGrado(tonica, g + off);
}
int acordeDe(uint32_t compas) {                     // grado del acorde que suena en ese compás
  const Estilo &E = ESTILOS[estilo];
  int total = 0;
  for (int k = 0; k < NUM_SEG && E.prog[k].compases; k++) total += E.prog[k].compases;
  int pos = (int)(compas % (uint32_t)total);
  for (int k = 0; k < NUM_SEG && E.prog[k].compases; k++) {
    if (pos < E.prog[k].compases) return E.prog[k].grado;   // tal cual: la tríada de la escala, sin reemplazos
    pos -= E.prog[k].compases;
  }
  return 0;
}

// Prioridad de cada golpe del patrón de batería, desde la letra: X entra primero, x después, los
// fantasmas al final. El azar dentro de cada letra es fijo por pista y paso (siempre el mismo
// patrón), así subir la densidad agrega golpes de a poco y nunca cambia los que ya sonaban.
void armarPrioridades() {
  const Estilo &E = ESTILOS[estilo];
  for (int t = 0; t < NUM_IND; t++) {
    const char *lin = E.bateria[C.patDrum][t + 1];
    for (int s = 0; s < 16; s++) {
      uint32_t h = (uint32_t)(t * 16 + s + 1) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
      float r = (h & 0xFFFF) / 65535.0f;
      char c = lin[s];
      prio[t][s] = c == 'X' ? 0.02f + 0.10f * r : (c == 'x' || c == 'r' || c == 't') ? 0.20f + 0.30f * r : c == '.' ? 0.55f + 0.40f * r : 9.0f;
    }
  }
  // PERCUSIÓN EXTRA: los huecos (sin bombo, caja ni clap) ordenados con un azar fijo por pista, estilo y
  // patrón. Cada pista toca sus primeros EXTRA_N·densidad: siempre hay golpes si hay huecos. Los largos
  // prefieren las corcheas.
  for (int e = 0; e < NUM_EXTRA; e++) {
    float clave[16]; int orden[16], n = 0;
    for (int st = 0; st < 16; st++) {
      rangoExtra[e][st] = 99;
      const bool hueco = E.bateria[C.patDrum][0][st] == '-' && E.bateria[C.patDrum][1 + I_CAJA][st] == '-' && E.bateria[C.patDrum][1 + I_CLAP][st] == '-';
      if (!hueco) continue;
      uint32_t h = (uint32_t)(st + 1 + 16 * e + 97 * estilo + 331 * C.patDrum) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
      clave[st] = (h & 0xFFFF) / 65535.0f + ((e == 2 && (st & 1)) ? 1.0f : 0.0f);
      orden[n++] = st;
    }
    for (int a = 1; a < n; a++) { int x = orden[a], b = a - 1; while (b >= 0 && clave[orden[b]] > clave[x]) { orden[b + 1] = orden[b]; b--; } orden[b + 1] = x; }
    for (int k = 0; k < n; k++) rangoExtra[e][orden[k]] = (uint8_t)k;
  }
  estiloPrio = estilo; patPrio = C.patDrum;
}

// ─── Disparos ──────────────────────────────────────────────
void publicarGolpe(int ev, float v) { velGolpe[ev] = v; golpes[ev] = golpes[ev] + 1; }
static inline float velDe(char c) { return c == 'X' ? 1.0f : (c == 'x' ? 0.80f : (c == 'r' || c == 't') ? 0.72f : 0.55f); }

void golpeBombo(float v) {
  const Kit &K = KITS[kit];
  VozBombo &a = bombos[sigBombo]; if (a.viva) { a.eC = a.kC = a.cC = dRapido3; }   // el parche anterior se calla en 3 ms
  sigBombo ^= 1; VozBombo &b = bombos[sigBombo];
  b.fObj = K.kFreq; b.f = K.kFreq * K.kRatio; b.fCoef = 1.0f - expf(-1.0f / (K.kDrop * SR));
  b.ph = 0.0f; b.env = 1.0f; b.knock = 1.0f; b.click = 1.0f; b.amp = v; b.viva = true;
  b.punch = K.kPunch; b.kAmt = K.kKnock; b.cAmt = K.kClick; b.sat = K.kSat;
  b.eC = decaimiento(K.kDec * (0.75f + 0.50f * (largoDrum - 0.5f))); b.kC = decaimiento(0.014f); b.cC = decaimiento(0.005f);
  bqBPF(b.bp, K.kKnockF, 0.8f); bqHPF(b.hp, 3000.0f, 0.7f);
  scObj = 1.0f;
  publicarGolpe(EV_BOMBO, v);
}

void golpeBajo(int nota) {
  float inc = hz((float)nota) / SR;
  bajo.incObj = inc;
  if (!C.glide || bajo.env < 1e-3f) bajo.inc1 = inc;   // con glide, la altura se desliza desde la nota anterior
  bajo.atk = true; bajo.atkF = true;               // sube desde donde estaba: sin clic, sin reiniciar la fase
  notaBajoPub = nota;
  publicarGolpe(EV_BAJO, 1.0f);
}

void golpeKeys(int nota, float vel) {               // KEYS: una sola voz; la fase no se reinicia, la amplitud sube desde donde estaba
  const TimbreMel &T = TMEL[tMel];
  float f = hz((float)nota);
  if (!kv.viva) { kv.f1 = 0.13f; kv.f2 = 0.61f; kv.amp = 0.0f; kv.s.ic1 = kv.s.ic2 = 0.0f; }
  kv.inc1 = f / SR; kv.inv1 = 1.0f / kv.inc1;
  kv.inc2 = kv.inc1 * (T.onda == 4 ? 1.4983f : T.det); kv.inv2 = 1.0f / kv.inc2;   // la 2.ª sierra, la quinta o el modulador FM
  kv.base = f * T.corte * filtMel; kv.fenv = 1.0f; kv.atk = true; kv.viva = true;
  kv.eC = decaimiento(decMel * (0.8f + 0.4f * vel));
  dKeysFenv = decaimiento(T.fenvTau);
  notaKeysPub = nota;
  publicarGolpe(EV_KEYS, vel);
}

void golpeArp(int nota) {                           // ARPEGIO: notas cortas del acorde, una voz por altura, alternando lados
  ladoArp ^= 1;
  float pan = ladoArp ? 0.28f : 0.72f;
  int k = -1;
  for (int i = 0; i < VOCES_ARP; i++) if (arps[i].viva && arps[i].nota == nota) k = i;
  if (k < 0) for (int i = 0; i < VOCES_ARP; i++) if (!arps[i].viva) { k = i; break; }
  if (k < 0) { float menor = 9.0f; for (int i = 0; i < VOCES_ARP; i++) if (arps[i].amp < menor) { menor = arps[i].amp; k = i; } }
  VozPluck &v = arps[k];
  float f = hz((float)nota);
  if (!v.viva) { v.f1 = 0.13f; v.amp = 0.0f; v.s.ic1 = v.s.ic2 = 0.0f; }
  v.inc1 = f / SR; v.inv1 = 1.0f / v.inc1;
  v.nota = nota; v.base = f * 1.5f * filtMel; v.oct = 2.0f; v.q = 0.9f * qMelMul; v.fenv = 1.0f; v.atk = true; v.viva = true;
  float dec = 0.45f * decMel; if (dec < 0.04f) dec = 0.04f; if (dec > 0.40f) dec = 0.40f;
  v.eC = decaimiento(dec);
  v.gL = sqrtf(1.0f - pan); v.gR = sqrtf(pan);
  notaArpPub = nota;
  publicarGolpe(EV_ARP, 0.7f);
}

void golpeInd(int t, float vel, bool acento, float largoExtra) {
  VozInd &a = ind[t][sigInd[t]];
  if (a.viva) { a.eC = a.nC = dRapido3; }          // fast-kill: el parche anterior se calla en 3 ms
  sigInd[t] ^= 1;
  VozInd &v = ind[t][sigInd[t]];
  memset(&v, 0, sizeof(v));
  const Golpe &g = KITS[kit].g[t];
  const float L = largoDrum * largoExtra;
  v.viva = true;
  v.amp = g.amp * vel;
  if (g.a0 > 0.0f) {                               // capa tonal (la caja, el perc grave, el tom)
    v.inc[0] = g.f / SR; v.a[0] = g.a0;
    if (g.r2 > 0.0f) { v.inc[1] = g.f * g.r2 / SR; v.a[1] = g.a1; }
    v.env = 1.0f; v.eC = decaimiento(g.eDec * L);
    v.cuanto = g.cuanto; v.caida = 1.0f; v.caidaC = decaimiento(g.caidaDec);
  }
  v.nz = 1.0f; v.nAmt = g.nAmt; v.nC = decaimiento((acento ? g.nAcc : g.nDec) * L);
  v.fc0 = g.fc0; v.fc1 = g.fc1; v.q = g.q;
  if (g.barre > 0.0f) v.barreInc = 1.0f / (g.barre * SR);
  if (g.hp > 0.0f) { bqHPF(v.banda, g.hp, 0.707f); v.conBanda = true; }
  coefFiltro(v.s, v.fc0, v.q);
  float pan = g.pan + g.panRnd * (2.0f * frnd() - 1.0f);
  pan = clamp01(pan);
  v.gL = sqrtf(1.0f - pan); v.gR = sqrtf(pan);
  publicarGolpe(t, vel);
}

// Un redoble: el primer golpe ya sonó; los otros n−1 caen repartidos dentro de la misma semicorchea
// (r = fusas, t = tresillo de fusas), cada uno un poco más suave. calcularBloque los dispara en su muestra.
void armarRedoble(int t, int n, float vel) {
  Redoble &r = redobles[t];
  r.quedan = n - 1; r.cada = largoPaso / n; r.falta = r.cada; r.vel = vel * 0.85f;
}

// SAMPLES: BTN1/BTN2/BTN3 disparan uno de su banco al azar (nunca el mismo dos veces seguidas). El que
// sonaba se apaga en 4 ms. Si un banco quedara vacío en samples.h, suena un impacto sintetizado.
void golpeMuestra(int banco) {
  VozMuestra &a = muestras[sigMuestra];
  if (a.viva) { a.obj = 0.0f; a.k = 1.0f - decaimiento(0.004f); }
  sigMuestra ^= 1;
  VozMuestra &v = muestras[sigMuestra];
  memset(&v, 0, sizeof(v));
  const int cat = banco, desde = SMP_INICIO[cat], n = SMP_INICIO[cat + 1] - desde;
  v.viva = true; v.reversa = false; v.obj = 1.0f; v.k = 1.0f - decaimiento(0.003f); v.bloque = -1;
  if (n > 0) {
    int k = (int)(frnd() * n); if (k >= n) k = n - 1;
    if (n > 1 && k == ultimaMuestra[cat]) k = (k + 1 + (int)(frnd() * (n - 1))) % n;
    ultimaMuestra[cat] = k; v.idx = desde + k; v.pos = 0;
    adpcmIniciar(v.dec, SMP_DATOS[v.idx], 0);
    v.pen = 0; v.ult = 0;
  } else {
    v.sintetica = true; v.sem = 0x2545F491u ^ muestraGlobal; v.f = 98.0f;
  }
  publicarGolpe(EV_MUESTRA, 1.0f);
}

// PERCUSIÓN EXTRA: un golpe de la pista e (dos voces que se turnan: la que sonaba se apaga en 3 ms)
void golpeExtra(int e, int r, float vel) {
  VozRecorte &a = extras[e][sigExtra[e]];
  if (a.viva) { a.obj = 0.0f; a.k = 1.0f - dRapido3; }
  sigExtra[e] ^= 1;
  VozRecorte &v = extras[e][sigExtra[e]];
  v.viva = true; v.r = r; v.pos = 0; v.amp = 1.0f; v.obj = 1.0f; v.k = 0.0f; v.vel = vel;
  v.gL = sqrtf(1.0f - EXTRA_PAN[e]); v.gR = sqrtf(EXTRA_PAN[e]);
  publicarGolpe(I_PERC, vel * 0.7f);
}

// ─── Un paso del secuenciador (semicorchea) ─────────────────
int notaBajoDe(char c, int raiz) {
  int off = 0;
  switch (c) { case 'o': off = 7; break; case '2': off = 1; break; case '3': off = 2; break; case '4': off = 3; break;
               case '5': off = 4; break; case '6': off = 5; break; case 'b': off = 6; break; }
  const int t = tonicaBajo + (C.octBajo ? 12 : 0);
  int n = notaAcorde(t, raiz, off, false);
  if (c == 'b') {                                  // séptima: sólo si es menor; si no, la quinta
    int f = notaAcorde(t, raiz, 0, false);
    if (((n - f) % 12 + 12) % 12 != 10) n = notaAcorde(t, raiz, 4, false);
  }
  return n;
}

void golpeCajaSubida(float vel, float largo) {
  cajaSub.env = 1.0f; cajaSub.nz = 1.0f; cajaSub.amp = vel;
  cajaSub.eC = decaimiento(0.05f * largo); cajaSub.nC = decaimiento(0.11f * largo);
  coefFiltro(cajaSub.s, 2200.0f, 1.0f);
}
void golpeFinal() {
  golpeFin.viva = true; golpeFin.f = 130.0f; golpeFin.env = 1.0f; golpeFin.nz = 1.0f;
  golpeFin.eC = decaimiento(0.45f); golpeFin.nC = decaimiento(0.30f);
  coefFiltro(golpeFin.s, 3200.0f, 0.6f);
  reverbViva = (int)(3.0f * SR);
}

// Posición VIRTUAL que leen todos los patrones. Normalmente es el reloj maestro; con el BEAT
// REPEAT apretado, repite el pulso donde lo apretaste: 2 vueltas de 4 semicorcheas, 4 de 2, y
// después una sola semicorchea — un redoble que se cierra. El reloj maestro nunca se detiene, así
// que al soltar todo vuelve exactamente a tiempo.
uint32_t posicionVirtual() {
  if (!C.repeat) return pasoTotal;
  uint32_t n = pasoTotal - repAncla;
  if (n < 8)  return repAncla + (n & 3);
  if (n < 16) return repAncla + (n & 1);
  return repAncla;
}

void avanzarPaso() {
  pasoTotal++;
  largoPaso = sps * ((pasoTotal & 1) ? 2.0f * (1.0f - swingS) : 2.0f * swingS);   // SWING: el par siempre suma dos
  const uint32_t pr = pasoTotal;                   // el reloj maestro
  const int sr = pr & 15;
  if (C.repeat && !repAntes) repAncla = pr & ~3u;  // el pulso que estaba sonando
  repAntes = C.repeat;
  const uint32_t vp = posicionVirtual();
  const int s = vp & 15;
  const uint32_t compas = vp >> 4;

  if (estadoSub == E_SUBIDA) {
    subPasos++;
    if (subPasos >= SUBIDA_PASOS && sr == 0) {     // fin en el "1": el golpe, y todo lo demás se corta
      estadoSub = E_ESPERA; golpeFinal();
    } else if (subPasos > SUBIDA_PASOS / 2) {      // caja desde la mitad: negras → corcheas → semicorcheas
      int k = subPasos - SUBIDA_PASOS / 2, cada = k <= 32 ? 4 : (k <= 64 ? 2 : 1);
      if ((sr % cada) == 0) golpeCajaSubida(0.30f + 0.70f * fminf(1.0f, (float)k / (SUBIDA_PASOS / 2)), cada == 4 ? 1.0f : cada == 2 ? 0.7f : 0.45f);
    }
  }
  const Estilo &E = ESTILOS[estilo];
  const int raiz = acordeDe(compas);
  acordePub = raiz;

  // ── MELODÍAS ──
  if (C.melOn) {
    char k = E.keys[C.patMel][(compas & 1) * 16 + s];
    int tk = TONICA_KEYS + transp; while (tk < KEYS_DESDE) tk += 12;
    tk += C.octMel == 1 ? 12 : (C.octMel == 2 ? -12 : 0);
    tk += 12 * E.octKeys;
    if (k >= '0' && k <= '9') golpeKeys(notaAcorde(tk, raiz, k - '1', true), (s & 3) == 0 ? 1.0f : 0.8f);
    if (C.arp) {
      char a = E.arp[C.patMel][s];
      int ta = TONICA_ARP + transp + (C.octMel == 1 ? 12 : (C.octMel == 2 ? -12 : 0));
      if (a >= '0' && a <= '5') golpeArp(notaAcorde(ta, raiz, TONOS_ARP[a - '0'], true));
    }
  }

  // ── BAJO ── (el break lo calla, como al bombo)
  if (C.bajoOn && !aBreak) {
    char c = E.bajo[C.patBajo][(compas & 3) == 3 ? 1 : 0][s];
    if (c != '-') golpeBajo(notaBajoDe(c, raiz));
  }

  // ── BATERÍA ──
  // Media velocidad (BTN5 del panel, mantenido): cada paso del patrón dura dos del reloj. El ancla
  // es donde lo apretaste, así al soltar el patrón sigue desde el reloj maestro, en tiempo.
  if (C.mitad && !mitadAntes) mitadAncla = (int32_t)vp;
  mitadAntes = C.mitad;
  int32_t dp = (int32_t)vp; bool suena = true;
  if (C.mitad) { int32_t d = (int32_t)vp - mitadAncla; if (d >= 0) { dp = mitadAncla + d / 2; suena = (d & 1) == 0; } }
  // FILL: los pasos 13–16 del compás pedido, en lugar del patrón (con el beat repeat no hay fill)
  bool enFill = fillPend && !C.repeat && (pr >> 4) == fillCompas && sr >= 12;
  if (fillPend && (pr >> 4) > fillCompas) fillPend = false;
  fillPub = enFill;
  if (sr == 15 && enFill) { fillPend = false; crashPend = true; }
  if (C.drumOn && suena) {
    const int ds = dp & 15;
    if (estiloPrio != estilo || patPrio != C.patDrum) armarPrioridades();
    char kc = enFill ? E.fill[0][sr - 12] : E.bateria[C.patDrum][0][ds];
    if (kc != '-' && C.kick && !aBreak) golpeBombo(velDe(kc));
    for (int t = 0; t < NUM_IND; t++) {
      if (enFill) {                                // el fill suena entero, sin mirar la densidad
        char c = E.fill[t + 1][sr - 12];
        if (c != '-') { golpeInd(t, velDe(c), c == 'X', 0.6f);   // golpes cortos: un redoble de golpes largos es barro
                        if (c == 'r' || c == 't') armarRedoble(t, c == 'r' ? 2 : 3, velDe(c)); }
        continue;
      }
      if (prio[t][ds] >= densS) continue;
      char c = E.bateria[C.patDrum][t + 1][ds];
      golpeInd(t, velDe(c) * (0.92f + 0.08f * frnd()), c == 'X', 1.0f);
      if (c == 'r' || c == 't') armarRedoble(t, c == 'r' ? 2 : 3, velDe(c));
    }
    // PERCUSIÓN EXTRA: sólo donde el patrón no tiene bombo, caja ni clap (derivada de lo escrito, nunca encima)
    if (C.extra > 0 && !enFill) {
      for (int e = 0; e < C.extra && e < NUM_EXTRA; e++) {
        const int rk = rangoExtra[e][ds];
        if (rk < 99 && (float)rk < EXTRA_N[e] * densS) {
          const int r = e * 4 + (int)((estilo * 3 + C.patDrum + e) & 3);   // el recorte de esta pista para este estilo y patrón
          golpeExtra(e, r, rk == 0 ? 1.0f : 0.80f - 0.08f * rk);
        }
      }
    }
    if (crashPend && sr == 0 && !enFill) { golpeInd(I_ABIERTO, 1.0f, true, 2.5f); crashPend = false; }   // el plato del "1"
  } else if (crashPend && sr == 0) crashPend = false;

  pasoTotalPub = pr; contadorPasos = contadorPasos + 1;
}

// ==============================================================================================================================================
// RENDER POR BLOQUES — cada voz de corrido, con su estado en variables locales
// ==============================================================================================================================================
IRAM_ATTR void renderExtra(float *oL, float *oR, const float *scb, int m) {
  for (int e = 0; e < NUM_EXTRA; e++) for (int k = 0; k < 2; k++) {
    VozRecorte &v = extras[e][k]; if (!v.viva) continue;
    const uint8_t *d = REC_DATOS[v.r]; const int32_t n = REC_LARGO[v.r];
    int32_t pos = v.pos; float amp = v.amp; const float obj = v.obj, kk = v.k;
    const float g = NIVEL_EXTRA * NIVEL_IND * v.vel, gL = v.gL * g, gR = v.gR * g;
    for (int i = 0; i < m; i++) {
      if (pos >= n) { v.viva = false; break; }     // el recorte ya trae su caída hasta cero
      amp += (obj - amp) * kk;
      const float y = MULAW[d[pos++]] * (1.0f / 32768.0f) * amp * (1.0f - SC_IND * scb[i]);
      oL[i] += y * gL; oR[i] += y * gR;
    }
    v.pos = pos; v.amp = amp;
    if (obj == 0.0f && amp < 1e-4f) v.viva = false;
  }
}

IRAM_ATTR void renderSidechain(float *o, int m) {  // baja en ~2 ms al llegar el bombo y vuelve en ~140 ms
  float obj = scObj, s = sc;
  for (int i = 0; i < m; i++) { obj *= dSc; s += (obj - s) * 0.012f; o[i] = s; }
  scObj = obj; sc = s;
}

IRAM_ATTR void renderBombo(float *o, int m) {
  uint32_t sem = semillaRuido;
  for (int k = 0; k < 2; k++) {
    VozBombo &v = bombos[k]; if (!v.viva) continue;
    float f = v.f, ph = v.ph, env = v.env, kn = v.knock, cl = v.click;
    const float eC = v.eC, kC = v.kC, cC = v.cC, amp = v.amp * NIVEL_BOMBO, fo = v.fObj, fk = v.fCoef;
    const float punch = v.punch, sat = v.sat, kA = v.kAmt * 1.6f, cA = v.cAmt * 0.45f, norm = 0.9f / (1.0f + 0.4f * punch);
    for (int i = 0; i < m; i++) {
      f += (fo - f) * fk;
      ph += f * (1.0f / SR); if (ph >= 1.0f) ph -= 1.0f;
      float p2 = ph + ph; if (p2 >= 1.0f) p2 -= 1.0f;
      float y = softClip((seno(ph) + punch * seno(p2)) * sat) * norm * env;
      if (kn > 1e-4f) y += pasoBQ(v.bp, ruido(sem)) * kn * kA;
      if (cl > 1e-4f) y += pasoBQ(v.hp, ruido(sem)) * cl * cA;
      o[i] += y * amp;
      env *= eC; kn *= kC; cl *= cC;
    }
    v.f = f; v.ph = ph; v.env = env; v.knock = kn; v.click = cl;
    if (env < 1e-3f) v.eC = dRapido;               // −60 dB: cola de 1.5 ms y recién a −82 dB se libera
    if (env < 8e-5f && kn < 1e-4f) v.viva = false;
  }
  semillaRuido = sem;
}

IRAM_ATTR void renderBajo(float *o, const float *scb, int m, float corte, float q, float comp) {
  if (!bajo.atk && bajo.env < 1e-5f) return;
  const TimbreBajo &T = TBAJO[tBajo];
  float f1 = bajo.f1, f2 = bajo.f2, fs = bajo.fs, fsn = bajo.fsn, env = bajo.env, envF = bajo.envF, inc = bajo.inc1;
  bool atk = bajo.atk, atkF = bajo.atkF;
  const float incObj = bajo.incObj, kg = C.glide ? kGlide16 : 1.0f;
  const bool dos = T.detune != 1.0f, conSierra = T.sierras > 0.0f, conSub = T.sub > 0.0f, conSeno = T.seno > 0.0f;
  const float drv = T.drive * (C.distBajo ? DIST_BAJO : 1.0f);
  const float nivel = NIVEL_BAJO * T.nivel * comp, dComp = 1.0f / (0.75f + 0.25f * drv);
  float i1 = inc, i2 = inc * T.detune, iS = inc * 0.5f, v1 = 1.0f / i1, v2 = 1.0f / i2, vS = 1.0f / iS;
  Filtro s = bajo.s; const uint32_t c = muestraGlobal;
  for (int i = 0; i < m; i++) {
    if (atk) { env += incAtkBajo; if (env >= 1.0f) { env = 1.0f; atk = false; } } else env *= dBajo;
    if (atkF) { envF += 1.0f / 66.0f; if (envF >= 1.0f) { envF = 1.0f; atkF = false; } } else envF *= dBajoF;
    if (((c + i) & 15) == 0) {
      inc += (incObj - inc) * kg;                   // glide: se desliza hacia la nota nueva (sin glide, salta)
      i1 = inc; i2 = inc * T.detune; iS = inc * 0.5f; v1 = 1.0f / i1; v2 = 1.0f / i2; vS = 1.0f / iS;
      coefFiltro(s, corte * exp2Rapido(T.envOct * envF), q);
    }
    float mez = 0.0f;
    if (conSierra) { float a = T.cuadrada ? cuadrada(f1, i1, v1) : sierra(f1, i1, v1); if (dos) a = (a + sierra(f2, i2, v2)) * 0.5f; mez += a * T.sierras; }
    if (conSub) mez += cuadrada(fs, iS, vS) * T.sub;
    if (conSeno) mez += suave(fsn, i1) * T.seno * 1.2f;
    float lp, bp; pasoFiltro(s, softClip(mez * drv) * dComp + 1e-18f, lp, bp);
    o[i] += lp * nivel * env * (1.0f - SC_BAJO * scb[i]);
  }
  bajo.f1 = f1; bajo.f2 = f2; bajo.fs = fs; bajo.fsn = fsn; bajo.env = env; bajo.envF = envF; bajo.atk = atk; bajo.atkF = atkF; bajo.s = s; bajo.inc1 = inc;
}

IRAM_ATTR void renderInd(float *oL, float *oR, const float *scb, int m) {
  uint32_t sem = semillaRuido;
  const uint32_t c = muestraGlobal;
  for (int t = 0; t < NUM_IND; t++) for (int k = 0; k < 2; k++) {
    VozInd &v = ind[t][k]; if (!v.viva) continue;
    float p0 = v.ph[0], p1 = v.ph[1];
    const float n0 = v.inc[0], n1 = v.inc[1], a0 = v.a[0], a1 = v.a[1];
    const bool dosParciales = a1 != 0.0f, conBanda = v.conBanda;
    float env = v.env, caida = v.caida, nz = v.nz, barre = v.barre;
    const float eC = v.eC, caidaC = v.caidaC, cuanto = v.cuanto, nC = v.nC, nAmt = v.nAmt, amp = v.amp * NIVEL_IND;
    const float gL = v.gL, gR = v.gR, barreInc = v.barreInc;
    Filtro s = v.s; BQ banda = v.banda;
    for (int i = 0; i < m; i++) {
      float y = 0.0f;
      if (env > 1e-5f) {                           // capa tonal
        float mul = 1.0f + cuanto * caida; caida *= caidaC;
        p0 += n0 * mul; if (p0 >= 1.0f) p0 -= 1.0f;
        float tt = seno(p0) * a0;
        if (dosParciales) { p1 += n1 * mul; if (p1 >= 1.0f) p1 -= 1.0f; tt += seno(p1) * a1; }
        y = tt * env;
        env *= eC;
      }
      if (barreInc > 0.0f && ((c + i) & 15) == 0) {   // barrido del filtro de ruido
        barre += barreInc * 16.0f; if (barre > 1.0f) barre = 1.0f;
        coefFiltro(s, v.fc0 + (v.fc1 - v.fc0) * barre, v.q);
      }
      if (nz > 1e-5f) {                            // capa de ruido
        float lp, bp; pasoFiltro(s, ruido(sem), lp, bp);
        y += bp * nz * nAmt; nz *= nC;
      }
      if (conBanda) y = pasoBQ(banda, y);
      y *= amp * (1.0f - SC_IND * scb[i]);
      oL[i] += y * gL; oR[i] += y * gR;
    }
    v.ph[0] = p0; v.ph[1] = p1; v.env = env; v.caida = caida; v.nz = nz; v.barre = barre;
    v.s = s; v.banda = banda;
    if (env < 1e-3f) v.eC = dRapido;               // −60 dB → 1.5 ms → se libera a −82 dB
    if (nz < 1e-3f) v.nC = dRapido;
    if (env < 8e-5f && nz < 8e-5f) v.viva = false;
  }
  semillaRuido = sem;
}

// ARPEGIO: una sierra PolyBLEP → pasa-bajos con envolvente (sube `oct` octavas y se cierra en
// ~70 ms) → VCA. Se apaga en dos etapas: −60 dB → 1.5 ms → libre a −82 dB.
IRAM_ATTR void renderArp(float *oL, float *oR, const float *scb, int m) {
  const uint32_t c = muestraGlobal;
  for (int k = 0; k < VOCES_ARP; k++) {
    VozPluck &v = arps[k]; if (!v.viva) continue;
    float f1 = v.f1, amp = v.amp, fenv = v.fenv; bool atk = v.atk; Filtro s = v.s; uint32_t sa = semArp;
    const float i1 = v.inc1, w1 = v.inv1, eC = v.eC, base = v.base, oct = v.oct, q = v.q;
    const float g = NIVEL_ARP * NIVEL_IND, gL = v.gL * g, gR = v.gR * g;
    for (int i = 0; i < m; i++) {
      if (atk) { amp += 1.0f / 132.0f; if (amp >= 1.0f) { amp = 1.0f; atk = false; } }   // 3 ms, desde donde estaba
      else { amp *= amp < 1e-3f ? dRapido : eC; if (amp < 8e-5f) { v.viva = false; break; } }
      fenv *= dArpFenv;
      if (((c + i + 7 * k) & 31) == 0) coefFiltro(s, base * exp2Rapido(oct * fenv), q);
      float lp, bp; pasoFiltro(s, (sierra(f1, i1, w1) + 0.22f * ruido(sa) * fenv) * 0.85f + 1e-18f, lp, bp);   // sierra con un soplo de ruido en el ataque
      float y = lp * amp * (1.0f - SC_IND * scb[i]);
      oL[i] += y * gL; oR[i] += y * gR;
    }
    v.f1 = f1; v.amp = amp; v.fenv = fenv; v.atk = atk; v.s = s; semArp = sa;
  }
}

// KEYS: voz (mono) → chorus estéreo → al bus de melodías; y un envío al eco ping-pong (a corchea con
// puntillo, sigue al tempo cruzando de cabezal en 50 ms). El eco devuelve a su propio bus, que NO
// pasa por el volumen ni el silencio de las melodías: así las colas suenan después de callarlas y
// el oscilador (que también le manda) no depende del panel de melodías.
IRAM_ATTR void renderKeys(float *oL, float *oR, float *eL, float *eR, const float *scb, const float *ecoExtra, float envioMel, int m) {
  if (kv.viva) {
    const TimbreMel &T = TMEL[tMel];
    float f1 = kv.f1, f2 = kv.f2, amp = kv.amp, fenv = kv.fenv, lfo = kv.lfo; bool atk = kv.atk; Filtro s = kv.s;
    const float i1 = kv.inc1, w1 = kv.inv1, i2 = kv.inc2, w2 = kv.inv2, eC = kv.eC, base = kv.base, dF = dKeysFenv, ia = incAtkMel;
    const float g = NIVEL_KEYS * NIVEL_IND * T.nivel * compMel, dLfo = KEYS_PWM_HZ / SR, envOct = T.envOct, q = fminf(10.0f, T.q * qMelMul);
    const uint8_t onda = T.onda;
    const uint32_t c = muestraGlobal;
    const float ruidoT = T.ruido, drv = T.drive * 0.75f, dComp = 1.0f / (0.75f + 0.25f * T.drive);
    const bool banda = T.banda;
    float fbk = kv.fb; uint32_t sem = semillaRuido;
    float iS = i1 * (2.0f + 5.0f * fenv), wS = 1.0f / iS;   // SYNC: la esclava sube con la envolvente (y un LFO lento)
    int i = 0;
    for (; i < m; i++) {
      if (atk) { amp += ia; if (amp >= 1.0f) { amp = 1.0f; atk = false; } }
      else { amp *= amp < 1e-3f ? dRapido : eC; if (amp < 8e-5f) { kv.viva = false; break; } }
      fenv *= dF;
      lfo += dLfo; if (lfo >= 1.0f) lfo -= 1.0f;
      if (((c + i) & 31) == 0) {
        coefFiltro(s, base * exp2Rapido(envOct * fenv), q);
        if (onda == 2) { iS = i1 * (1.5f + 5.0f * fenv + 0.5f + 0.5f * seno(lfo)); if (iS > 0.45f) iS = 0.45f; wS = 1.0f / iS; }
      }
      float x;
      switch (onda) {
        case 0:  x = sierra(f1, i1, w1); break;
        case 2: {                                    // SYNC duro: la esclava vuelve a cero con cada vuelta de la maestra
          f1 += i1; if (f1 >= 1.0f) { f1 -= 1.0f; f2 = f1 * (iS / i1); }
          x = sierra(f2, iS, wS); } break;
        case 3: {                                    // FM de fase, modulador con realimentación (rasposo)
          f2 += i2; if (f2 >= 1.0f) f2 -= 1.0f;
          const float mp = vuelta(f2 + fbk * MEL_FM_FB);
          const float mo = seno(mp); fbk = mo;
          f1 += i1; if (f1 >= 1.0f) f1 -= 1.0f;
          const float cp = vuelta(f1 + MEL_FM_IDX * (0.3f + fenv) * mo);
          x = seno(cp); } break;
        default: x = (sierra(f1, i1, w1) + sierra(f2, i2, w2)) * 0.6f; break;   // dos sierras · QUINTAS
      }
      if (ruidoT > 0.0f) x += ruidoT * ruido(sem) * (0.4f + fenv);
      float lp, bp; pasoFiltro(s, sat3(x * drv) * dComp + 1e-18f, lp, bp);   // la saturación, antes del filtro
      keysBuf[i] = (banda ? bp * 1.8f : lp) * amp * g * (1.0f - SC_IND * scb[i]);
    }
    for (; i < m; i++) keysBuf[i] = 0.0f;
    kv.f1 = f1; kv.f2 = f2; kv.amp = amp; kv.fenv = fenv; kv.lfo = lfo; kv.atk = atk; kv.s = s; kv.fb = fbk; semillaRuido = sem;
  } else for (int i = 0; i < m; i++) keysBuf[i] = 0.0f;

  const float dChorus = CHORUS_HZ / SR, base = CHORUS_MS * 0.001f * SR, prof = CHORUS_PROF_MS * 0.001f * SR;
  int objetivo = (int)(3.0f * sps); if (objetivo > ECO_MAX - 2) objetivo = ECO_MAX - 2;
  if (ecoXf >= 1.0f && abs(objetivo - ecoN) > 32) { ecoN2 = objetivo; ecoXf = 0.0f; }
  float lfoC = chorusLfo, xf = ecoXf, lpL = ecoLpL, lpR = ecoLpR, hpL = ecoHpL, hpR = ecoHpR;
  int ci = chorusIdx, ei = ecoIdx;
  float d1 = base + prof * seno(lfoC), d2 = base + prof * seno(lfoC + 0.25f >= 1.0f ? lfoC - 0.75f : lfoC + 0.25f);
  const uint32_t c0 = muestraGlobal;
  for (int i = 0; i < m; i++) {
    const float x = keysBuf[i];
    chorusBuf[ci] = x;
    lfoC += dChorus; if (lfoC >= 1.0f) lfoC -= 1.0f;
    if (((c0 + i) & 15) == 0) {
      d1 = base + prof * seno(lfoC); d2 = base + prof * seno(lfoC + 0.25f >= 1.0f ? lfoC - 0.75f : lfoC + 0.25f);
    }
    float p1 = (float)ci - d1; if (p1 < 0.0f) p1 += CHORUS_N;
    float p2 = (float)ci - d2; if (p2 < 0.0f) p2 += CHORUS_N;
    int a1 = (int)p1, a2 = (int)p2; float t1 = p1 - a1, t2 = p2 - a2;
    a1 &= CHORUS_N - 1; a2 &= CHORUS_N - 1;         // ci − d + N puede redondear a N justo
    float c1 = chorusBuf[a1] + (chorusBuf[(a1 + 1) & (CHORUS_N - 1)] - chorusBuf[a1]) * t1;
    float c2 = chorusBuf[a2] + (chorusBuf[(a2 + 1) & (CHORUS_N - 1)] - chorusBuf[a2]) * t2;
    ci = (ci + 1) & (CHORUS_N - 1);
    float L = x * (1.0f - CHORUS_MEZCLA * 0.5f) + c1 * CHORUS_MEZCLA, R = x * (1.0f - CHORUS_MEZCLA * 0.5f) + c2 * CHORUS_MEZCLA;

    int r1 = ei - ecoN; if (r1 < 0) r1 += ECO_MAX;
    float dl = ecoL[r1] * (1.0f / 32768.0f), dr = ecoR[r1] * (1.0f / 32768.0f);
    if (xf < 1.0f) {
      int r2 = ei - ecoN2; if (r2 < 0) r2 += ECO_MAX;
      float nl = ecoL[r2] * (1.0f / 32768.0f), nr = ecoR[r2] * (1.0f / 32768.0f);
      dl += (nl - dl) * xf; dr += (nr - dr) * xf;
      xf += 1.0f / 2205.0f; if (xf >= 1.0f) { xf = 1.0f; ecoN = ecoN2; }
    }
    lpL += (dl - lpL) * 0.45f; lpR += (dr - lpR) * 0.45f;      // cada vuelta más oscura (~4 kHz)
    hpL += (lpL - hpL) * 0.010f; hpR += (lpR - hpR) * 0.010f;  // y sin barro abajo (~70 Hz)
    float fl = lpL - hpL, fr = lpR - hpR;
    float inL = x * envioMel + ecoExtra[i] + ECO_FEEDBACK * fr, inR = ECO_FEEDBACK * fl;   // ping-pong: lo que vuelve cruza de lado
    if (inL > 0.99f) inL = 0.99f; if (inL < -0.99f) inL = -0.99f;
    if (inR > 0.99f) inR = 0.99f; if (inR < -0.99f) inR = -0.99f;
    ecoL[ei] = (int16_t)(inL * 32767.0f); ecoR[ei] = (int16_t)(inR * 32767.0f);
    ei = (ei + 1) & (ECO_MAX - 1);
    oL[i] += L; oR[i] += R;
    eL[i] = dl * ECO_MEZCLA; eR[i] = dr * ECO_MEZCLA;
  }
  chorusLfo = lfoC; chorusIdx = ci; ecoIdx = ei; ecoXf = xf; ecoLpL = lpL; ecoLpR = lpR; ecoHpL = hpL; ecoHpR = hpR;
}

// SUBIDA: oscilador agresivo que sube SUBIDA_OCTAVAS + ruido blanco, con trémolo desde la mitad
IRAM_ATTR void renderSubida(float *oL, float *oR, int m) {
  if (estadoSub != E_SUBIDA) return;
  const float porPaso = 1.0f / sps;
  const float fBaja = hz((float)tonicaBajo);
  float f1 = subF1, f2 = subF2, lp = subLp, tg = subTrem;
  float i1 = 0.001f, i2 = 0.001f, w1 = 1000.0f, w2 = 1000.0f;
  uint32_t a = semRuidoL, b = semRuidoR;
  const uint32_t c = muestraGlobal;
  for (int i = 0; i < m; i++) {
    float pos = (float)subPasos - (faltanTramo - (float)i) * porPaso;
    if (pos < 0.0f) pos = 0.0f;
    float p = pos / (float)SUBIDA_PASOS; if (p > 1.0f) p = 1.0f;
    if (i == 0 || ((c + i) & 15) == 0) {
      float f = fBaja * exp2Rapido(SUBIDA_OCTAVAS * p * (0.55f + 0.45f * p));
      i1 = f * 0.9914f / SR; i2 = f * 1.0087f / SR;
      w1 = 1.0f / i1; w2 = 1.0f / i2;
    }
    float objT = 1.0f;
    if (pos >= SUBIDA_PASOS / 2) {
      float ph = 1.0f - (faltanTramo - (float)i) * porPaso;
      float prof = fminf(1.0f, (pos - SUBIDA_PASOS / 2) / 16.0f) * 0.95f;
      objT = ph < 0.5f ? 1.0f : 1.0f - prof;
    }
    tg += (objT - tg) * 0.02f;
    float x = softClip((sierra(f1, i1, w1) + sierra(f2, i2, w2)) * 0.5f * SUBIDA_DRIVE);
    lp += (x - lp) * 0.55f;
    float o = lp * NIVEL_SUBIDA_OSC * (0.35f + 0.65f * p) * NIVEL_IND;
    float rn = NIVEL_SUBIDA_RUIDO * p * p * NIVEL_IND;
    oL[i] += (o + ruido(a) * rn) * tg;
    oR[i] += (o + ruido(b) * rn) * tg;
    if (cajaSub.env > 1e-4f || cajaSub.nz > 1e-4f) {
      cajaSub.ph1 += 190.0f / SR; if (cajaSub.ph1 >= 1.0f) cajaSub.ph1 -= 1.0f;
      cajaSub.ph2 += 297.0f / SR; if (cajaSub.ph2 >= 1.0f) cajaSub.ph2 -= 1.0f;
      float lpN, bpN; pasoFiltro(cajaSub.s, ruido(a), lpN, bpN);
      float y = ((seno(cajaSub.ph1) + 0.6f * seno(cajaSub.ph2)) * 0.45f * cajaSub.env + bpN * 1.3f * cajaSub.nz) * cajaSub.amp * NIVEL_CAJA_SUBIDA * NIVEL_IND;
      oL[i] += y; oR[i] += y;
      cajaSub.env *= cajaSub.eC; cajaSub.nz *= cajaSub.nC;
    }
  }
  subF1 = f1; subF2 = f2; subLp = lp; subTrem = tg; semRuidoL = a; semRuidoR = b;
}

// OSCILADOR: dos osciladores ±6 cents, cada uno con su SVF que sigue a la nota y que barre el LFO
// libre; chorus estéreo propio y envío al eco del keys. POT2 = dónde reposa el filtro (1·f … 16·f),
// POT3 = cuántas octavas lo mueve el LFO (0 = quieto), POT4 = volumen (el envío al delay es fijo). La resonancia va atada
// a la apertura: alta cerrado, baja abierto (nunca un pico chillón arriba).
IRAM_ATTR void renderOsc(float *oL, float *oR, float *eco, const float *scb, int m) {
  if (!osc.vivo) {                                 // callado: el retardo del chorus sigue corriendo vacío
    for (int i = 0; i < m; i++) { oL[i] = 0.0f; oR[i] = 0.0f; eco[i] = 0.0f; oscChorusBuf[oscChorusIdx] = 0.0f; oscChorusIdx = (oscChorusIdx + 1) & (OSC_CHORUS_N - 1); }
    return;
  }
  float f1 = osc.f1, f2 = osc.f2, inc = osc.inc, amp = osc.amp; Filtro s = osc.s, s2 = osc.s2;
  const float lado = 0.5f + 0.5f * OSC_ANCHO, otro = 1.0f - lado;
  const float incObj = osc.incObj, obj = osc.obj, k = obj > amp ? kOscAtk : kOscSuelta, g = NIVEL_OSC * NIVEL_IND;
  float cEnv = oscCompEnv, cG = oscCompG; const float trim = NIVEL_TOSC[tOsc];
  const float octBase = 4.0f * oscFiltS, octLfoP = OSC_LFO_OCT * oscLfoS, envio = OSC_ECO_FIJO, vol = OSC_VOL_MAX * ganVol(oscVolS);
  const int onda = tOsc;
  float i1 = inc / OSC_DETUNE, i2 = inc * OSC_DETUNE, w1 = 1.0f / i1, w2 = 1.0f / i2;
  const uint32_t c = muestraGlobal;
  float comp = osc.comp;
  float m1 = osc.m1, y1 = osc.y1;
  const float drv = OSC_DRIVE[onda] * 0.75f, dComp = 1.0f / (0.75f + 0.25f * OSC_DRIVE[onda]);
  const float fbFM = OSC_FM_FB * 0.159155f * (onda == 2 ? 1.3f : 1.0f);
  float idxFM = OSC_FM_IDX * 0.159155f;
  int ci = oscChorusIdx; float lfoC = oscChorusLfo;
  const float baseC = OSC_CHORUS_MS * 0.001f * SR, profC = OSC_CHORUS_PROF_MS * 0.001f * SR;
  float dC1 = baseC + profC * seno(lfoC), dC2 = baseC + profC * seno(lfoC + 0.25f >= 1.0f ? lfoC - 0.75f : lfoC + 0.25f);
  for (int i = 0; i < m; i++) {
    amp += (obj - amp) * k;
    inc += (incObj - inc) * dOscGlide;
    if (i == 0 || ((c + i) & 15) == 0) {
      i1 = inc / OSC_DETUNE; i2 = inc * OSC_DETUNE; w1 = 1.0f / i1; w2 = 1.0f / i2;
      if (((c + i) & 15) == 0) {
        lfoFalta -= 16.0f / SR;
        if (lfoFalta <= 0.0f) {
          lfoVelObj = (0.5f + 0.5f * ruido(semLfo)) * octLfo;
          lfoFalta = OSC_LFO_CAMBIO_MIN + (OSC_LFO_CAMBIO_MAX - OSC_LFO_CAMBIO_MIN) * (0.5f + 0.5f * ruido(semLfo));
        }
        lfoVel += (lfoVelObj - lfoVel) * kDeslizLfo;
        lfoFase += OSC_LFO_MIN_HZ * exp2Rapido(lfoVel) * (16.0f / SR); if (lfoFase >= 1.0f) lfoFase -= 1.0f;
      }
      float lfo = 0.5f + 0.5f * seno(lfoFase);
      idxFM = OSC_FM_IDX * 0.159155f * (onda == 2 ? 1.5f : 1.0f) * (0.2f + 1.6f * lfo);   // la FM respira con el mismo LFO del filtro
      float oct = octBase + octLfoP * lfo;                        // octavas sobre la nota
      float ab = oct * (1.0f / 7.5f); if (ab > 1.0f) ab = 1.0f;    // 0 cerrado · 1 abierto
      float q = OSC_Q_CERRADO + (OSC_Q_ABIERTO - OSC_Q_CERRADO) * ab;
      coefFiltro(s, inc * SR * exp2Rapido(oct), q);
      s2.a1 = s.a1; s2.a2 = s.a2; s2.a3 = s.a3; s2.k = s.k;
      comp = 1.0f / sqrtf(sqrtf(q));
    }
    float x1, x2;
    if (onda == 1)      { x1 = 0.75f * sierra(f1, i1, w1); x2 = 0.75f * sierra(f2, i2, w2); }
    else if (onda == 3) { x1 = 0.70f * cuadrada(f1, i1, w1); x2 = 0.70f * cuadrada(f2, i2, w2); }
    else {
      // FM de razón 1 con realimentación: el modulador va a la misma altura (afinado) y el portador comparte
      // la fase de la primera sierra. UN solo operador para los dos osciladores (el segundo lo toma a 6
      // cents de su sierra): dos operadores costaban 9 k ciclos por bloque más que todo el oscilador del v2.
      m1 += i1; if (m1 >= 1.0f) m1 -= 1.0f;
      y1 = seno(vuelta(m1 + y1 * fbFM));
      const float s1 = sierra(f1, i1, w1), s2v = sierra(f2, i2, w2);
      const float c1 = seno(vuelta(f1 + idxFM * y1));
      if (onda == 0) { x1 = 0.5f * (s1 + c1); x2 = 0.5f * (s2v + c1); }        // GRUÑIDO
      else           { x1 = 0.9f * c1; x2 = 0.55f * c1 + 0.35f * s2v; }         // FM
    }
    x1 = sat3(x1 * drv) * dComp; x2 = sat3(x2 * drv) * dComp;   // saturado ANTES del filtro
    float lp1, lp2, bp;
    pasoFiltro(s,  x1 + 1e-18f, lp1, bp);
    pasoFiltro(s2, x2 + 1e-18f, lp2, bp);
    // compresor: detecta ANTES del fundido de encendido y del sidechain, para no deshacerlos
    const float a0 = g * comp * 0.5f;
    lp1 *= a0; lp2 *= a0;
    const float pk = fabsf(lp1 + lp2);
    cEnv += (pk - cEnv) * (pk > cEnv ? kOscCompAtk : kOscCompRel);
    cG += (OSC_COMP_NIVEL / (cEnv + OSC_COMP_NIVEL * OSC_COMP_RODILLA) - cG) * 0.003f;
    const float a = cG * trim * amp * vol * (1.0f - OSC_SC * scb[i]);
    lp1 *= a; lp2 *= a;
    const float mono = lp1 + lp2;
    oscChorusBuf[ci] = mono;
    if (((c + i) & 15) == 0) {
      lfoC += OSC_CHORUS_HZ * 16.0f / SR; if (lfoC >= 1.0f) lfoC -= 1.0f;
      dC1 = baseC + profC * seno(lfoC); dC2 = baseC + profC * seno(lfoC + 0.25f >= 1.0f ? lfoC - 0.75f : lfoC + 0.25f);
    }
    float p1 = (float)ci - dC1; if (p1 < 0.0f) p1 += OSC_CHORUS_N;
    float p2 = (float)ci - dC2; if (p2 < 0.0f) p2 += OSC_CHORUS_N;
    int a1 = (int)p1, a2 = (int)p2; float t1 = p1 - a1, t2 = p2 - a2;
    a1 &= OSC_CHORUS_N - 1; a2 &= OSC_CHORUS_N - 1;
    float c1 = oscChorusBuf[a1] + (oscChorusBuf[(a1 + 1) & (OSC_CHORUS_N - 1)] - oscChorusBuf[a1]) * t1;
    float c2 = oscChorusBuf[a2] + (oscChorusBuf[(a2 + 1) & (OSC_CHORUS_N - 1)] - oscChorusBuf[a2]) * t2;
    ci = (ci + 1) & (OSC_CHORUS_N - 1);
    const float mz = OSC_CHORUS_MEZCLA * 0.5f, seco = 1.0f - 0.5f * OSC_CHORUS_MEZCLA;
    oL[i] = (lp1 * lado + lp2 * otro) * seco + c1 * mz;
    oR[i] = (lp2 * lado + lp1 * otro) * seco + c2 * mz;
    eco[i] = mono * envio;
  }
  oscChorusIdx = ci; oscChorusLfo = lfoC;
  oscCompEnv = cEnv; oscCompG = cG;
  osc.f1 = f1; osc.f2 = f2; osc.inc = inc; osc.amp = amp; osc.s = s; osc.s2 = s2; osc.comp = comp;
  osc.m1 = m1; osc.y1 = y1;
  if (obj == 0.0f && amp < 1e-5f) osc.vivo = false;   // ya a −100 dB: se libera sin escalón
}

// GOLPE FINAL + REVERB: bus propio que el corte del final de la subida no toca
static inline float peine(float *b, int n, int &i, float &lp, float x) {
  float y = b[i]; lp += (y - lp) * 0.45f;
  b[i] = x + lp * REVERB_FB; if (++i >= n) i = 0; return y;
}
static inline float pasaTodo(float *b, int n, int &i, float x) {
  float d = b[i], y = d - 0.5f * x; b[i] = x + 0.5f * d; if (++i >= n) i = 0; return y;
}
IRAM_ATTR void renderGolpe(float *oL, float *oR, const float *rvIn, int m) {
  if (!golpeFin.viva && reverbViva <= 0) { for (int i = 0; i < m; i++) { oL[i] = 0.0f; oR[i] = 0.0f; } return; }
  uint32_t sem = semRuidoR;
  const float kIn = 0.28f * (1.0f - REVERB_FB);
  for (int i = 0; i < m; i++) {
    float g = 0.0f;
    if (golpeFin.viva) {
      golpeFin.f += (40.0f - golpeFin.f) * 0.0012f;
      golpeFin.ph += golpeFin.f / SR; if (golpeFin.ph >= 1.0f) golpeFin.ph -= 1.0f;
      float lpN, bpN; pasoFiltro(golpeFin.s, ruido(sem), lpN, bpN);
      g = (softClip(seno(golpeFin.ph) * 1.6f) * golpeFin.env + bpN * 0.9f * golpeFin.nz) * NIVEL_GOLPE * NIVEL_IND;
      golpeFin.env *= golpeFin.eC; golpeFin.nz *= golpeFin.nC;
      if (golpeFin.env < 1e-4f && golpeFin.nz < 1e-4f) golpeFin.viva = false;
    }
    float x = (g * GOLPE_REVERB + rvIn[i]) * kIn / 0.28f;   // el golpe final y el envío de los samples
    float cc = peine(rvC1, RV_N1, rvI1, rvLp1, x) + peine(rvC2, RV_N2, rvI2, rvLp2, x)
             + peine(rvC3, RV_N3, rvI3, rvLp3, x) + peine(rvC4, RV_N4, rvI4, rvLp4, x);
    float wl = pasaTodo(rvA2, RV_A2, rvJ2, pasaTodo(rvA1, RV_A1, rvJ1, cc));
    float wr = pasaTodo(rvA4, RV_A4, rvJ4, pasaTodo(rvA3, RV_A3, rvJ3, cc));
    oL[i] = g + wl * 2.2f; oR[i] = g + wr * 2.2f;
  }
  semRuidoR = sem;
  reverbViva -= m;
}

// SAMPLES: la muestra de la flash leída al derecho o al revés a la velocidad del pitch (interpolación
// lineal), con fundidos de 3–4 ms y sus envíos a la reverb y al eco. Si una categoría quedara vacía: un
// impacto — un sub que cae de 98 a 28 Hz y un golpe de ruido que se oscurece.
IRAM_ATTR void renderMuestras(float *oL, float *oR, float *rv, float *eco, int m) {
  for (int i = 0; i < m; i++) { oL[i] = 0.0f; oR[i] = 0.0f; rv[i] = 0.0f; eco[i] = 0.0f; }
  if (!muestras[0].viva && !muestras[1].viva) return;
  const float niv = NIVEL_MUESTRA * gMuestraB, revV = revMuestraS, ecoV = ecoMuestraS;
  for (int k = 0; k < VOCES_MUESTRA; k++) {
    VozMuestra &v = muestras[k]; if (!v.viva) continue;
    float amp = v.amp, lp = v.lp; const float obj = v.obj, kk = v.k;
    float paso = pasoMuestra;
    if (!v.sintetica) {
      // pos cuenta en el orden en que suena: al derecho es la muestra pos; al revés, la n−1−pos.
      const uint8_t *datos = SMP_DATOS[v.idx];
      const int32_t n = (int32_t)SMP_LARGO[v.idx];
      const bool mu = SMP_FORMATO[v.idx] == 1;
      int32_t pos = v.pos, pen = v.pen, ult = v.ult; float fr = v.frac;
      const int32_t borde = (int32_t)(220.0f * paso) + 2;
      for (int i = 0; i < m; i++) {
        fr += paso; int adv = (int)fr; fr -= adv; pos += adv;
        if (pos >= n - 1) { v.viva = false; break; }
        float a0, a1;
        if (!v.reversa) {
          if (mu) { a0 = MULAW[datos[pos]]; a1 = MULAW[datos[pos + 1]]; }
          else {
            while ((int32_t)v.dec.n < pos + 2) { pen = ult; ult = adpcmSig(v.dec); }   // de corrido: sin picos de trabajo
            a0 = (float)pen; a1 = (float)ult;
          }
        } else {
          const int32_t r = n - 1 - pos;           // la muestra real, yendo hacia atrás
          if (mu) { a0 = MULAW[datos[r]]; a1 = MULAW[datos[r - 1]]; }
          else {
            const int b = r >> 10;
            if (b != v.bloque) {                   // bloque nuevo: se decodifica entero (23 ms de audio)
              Adpcm d; adpcmIniciar(d, datos, b);
              for (int k = 0; k < ADPCM_BLOQUE; k++) v.buf[k] = (int16_t)adpcmSig(d);
              v.bloque = b;
            }
            a0 = v.buf[r & 1023]; a1 = (r & 1023) ? v.buf[(r - 1) & 1023] : a0;
          }
        }
        float x = (a0 + (a1 - a0) * fr) * (1.0f / 32768.0f);
        const int32_t falta = n - 1 - pos;
        const float fin = falta < borde ? (float)falta / (float)borde : 1.0f;
        amp += (obj - amp) * kk;
        lp += (x - lp) * 0.55f;                    // pasa-bajos suave: el pitch hacia arriba no chilla
        float y = lp * amp * fin * niv;
        oL[i] += y; oR[i] += y; rv[i] += y * revV; eco[i] += y * ecoV;
      }
      v.pos = pos; v.frac = fr; v.pen = pen; v.ult = ult;
    } else {
      uint32_t sem = v.sem; float t = v.t, ph = v.ph; Filtro fz = v.fz;
      const float dur = IMPACTO_S * SR;
      float eS = 0.0f, eN = 0.0f, f = 0.0f;
      for (int i = 0; i < m; i++) {
        float p = t / dur; if (p >= 1.0f) { v.viva = false; break; }
        if (i == 0 || (i & 15) == 0) {
          float q = v.reversa ? 1.0f - p : p, tt = q * IMPACTO_S;
          f = (28.0f + 70.0f * expf(-tt / 0.35f)) * paso;
          eS = expf(-tt / 0.9f) * fminf(1.0f, tt * 1000.0f); eN = expf(-tt / 0.35f);
          if (v.reversa) { float r = (1.0f - p) * IMPACTO_S; eS *= fminf(1.0f, r * 20.0f); }
          coefFiltro(fz, (300.0f + 6000.0f * expf(-tt / 0.12f)) * fminf(2.0f, paso), 0.8f);
        }
        ph += f / SR; if (ph >= 1.0f) ph -= 1.0f;
        float lpN, bpN; pasoFiltro(fz, ruido(sem), lpN, bpN);
        amp += (obj - amp) * kk;
        float y = softClip((seno(ph) * 1.2f * eS + lpN * 0.8f * eN) * 2.0f) * 0.40f * amp * niv;   // el impacto sintetizado, al nivel de un sample de la tarjeta
        oL[i] += y; oR[i] += y; rv[i] += y * revV; eco[i] += y * ecoV;
        t += 1.0f;
      }
      v.sem = sem; v.t = t; v.ph = ph; v.fz = fz;
    }
    v.amp = amp; v.lp = lp;
    if (obj == 0.0f && amp < 1e-4f) v.viva = false;
  }
}

volatile uint32_t medSec[6];                        // MEDIR: ciclos por sección (batería · bajo · melodías · oscilador · master · pasos)
#if MEDIR && !defined(SIMULADOR)
#define CICLOS() ESP.getCycleCount()
#else
#define CICLOS() 0u
#endif
float envioMelBloque = 1.0f;
void renderTramo(int off, int m, float corte, float q, float comp) {
  uint32_t c0 = CICLOS();
  renderSidechain(bufSc + off, m);
  renderBombo(bufKick + off, m);
  renderInd(bufIndL + off, bufIndR + off, bufSc + off, m);
  renderExtra(bufIndL + off, bufIndR + off, bufSc + off, m);
  uint32_t c1 = CICLOS();
  renderBajo(bufBajo + off, bufSc + off, m, corte, q, comp);
  uint32_t c2 = CICLOS();
  renderOsc(oscL + off, oscR + off, oscEco + off, bufSc + off, m);   // antes de las melodías: le manda su envío al eco
  renderMuestras(smpL + off, smpR + off, smpRev + off, smpEco + off, m);
  if (muestras[0].viva || muestras[1].viva) for (int i = 0; i < m; i++) oscEco[off + i] += smpEco[off + i];   // los samples usan el mismo eco
  if ((muestras[0].viva || muestras[1].viva) && revMuestraS > 0.01f) reverbViva = (int)(3.0f * SR);
  uint32_t c3 = CICLOS();
  renderKeys(bufMelL + off, bufMelR + off, ecoBufL + off, ecoBufR + off, bufSc + off, oscEco + off, envioMelBloque, m);
  renderArp(bufMelL + off, bufMelR + off, bufSc + off, m);
  renderSubida(bufFxL + off, bufFxR + off, m);
  renderGolpe(golpeL + off, golpeR + off, smpRev + off, m);
  uint32_t c4 = CICLOS();
  medSec[0] += (c1 - c0) >> 6; medSec[1] += (c2 - c1) >> 6; medSec[3] += (c3 - c2) >> 6; medSec[2] += (c4 - c3) >> 6;
  muestraGlobal += m;
}

// ==============================================================================================================================================
// UN BLOQUE DE AUDIO (128 muestras ≈ 2.9 ms)
// ==============================================================================================================================================
static i2s_chan_handle_t tx_chan;
int16_t salida[BUFFER_SAMPLES * 2];

IRAM_ATTR void calcularBloque() {
  // ── Borde del buffer: aplicar los pedidos de la tarea de control ──
  memcpy(&C, (const void *)&reqC, sizeof(C));
  if (C.tocar && !aTocando) { pasoTotal = 0xFFFFFFFFu; faltan = 0.0f; repAntes = false; mitadAntes = false; fillPend = false; crashPend = false;
                              for (int t = 0; t < NUM_IND; t++) redobles[t].quedan = 0; }   // Play: el "1" en la primera muestra
  aTocando = C.tocar; tocandoPub = C.tocar;
  if (!C.tocar) estadoSub = E_NORMAL;
  if (reqSubida != vistoSubida) {
    vistoSubida = reqSubida;
    if (estadoSub == E_NORMAL && aTocando) { estadoSub = E_SUBIDA; subPasos = 0; subTrem = 1.0f; subLp = 0.0f; }
  }
  if (reqDrop != vistoDrop) {                      // el DROP: todo desde el "1", con bombo y bajo
    vistoDrop = reqDrop;
    if (estadoSub != E_NORMAL) {
      estadoSub = E_NORMAL;
      pasoTotal = 0xFFFFFFFFu; faltan = 0.0f;
      memset(ecoL, 0, sizeof(ecoL)); memset(ecoR, 0, sizeof(ecoR)); memset(chorusBuf, 0, sizeof(chorusBuf));
      kv.viva = false; kv.amp = 0.0f;
      for (int k = 0; k < VOCES_ARP; k++) arps[k].viva = false;
      for (int t = 0; t < NUM_IND; t++) for (int k = 0; k < 2; k++) ind[t][k].viva = false;
    }
  }
  estadoSubPub = estadoSub;
  aBreak = C.brk || estadoSub != E_NORMAL; breakPub = aBreak; repeatPub = C.repeat;
  if (reqFill != vistoFill) {                      // FILL: en el compás que suena si aún falta para el 13, si no en el siguiente
    vistoFill = reqFill;
    uint32_t p = pasoTotal + 1;                    // el próximo paso
    fillCompas = (p >> 4) + ((p & 15) > 11 ? 1 : 0); fillPend = true;
  }
  if (reqFillNo != vistoFillNo) { vistoFillNo = reqFillNo; fillPend = false; }
  for (int b = 0; b < NUM_BANCOS; b++) if (reqBanco[b] != vistoBanco[b]) { vistoBanco[b] = reqBanco[b]; golpeMuestra(b); }

  estilo = C.estilo; kit = C.kit; tBajo = C.tBajo; tMel = C.tMel; tOsc = C.tOsc;
  modo = ESTILOS[estilo].escala; transp = ESTILOS[estilo].transp;
  tonicaBajo = TONICA_BAJO + transp; if (tonicaBajo < 28) tonicaBajo += 12;   // nunca abajo de Mi1
  tonicaPub = TONICA_OSC + transp;

  // ── Pots suavizados (por bloque; las ganancias se interpolan por muestra) ──
  float b = BPM_MIN + BPM_RANGO * pPot[P_GENERAL][1];
  if (fabsf(b - bpm) > 0.6f) setBPM(roundf(b));   // BPM entero con histéresis: el ruido del ADC no hace bailar el tempo
  swingS += ((0.5f + 0.20f * pPot[P_GENERAL][2]) - swingS) * 0.2f;
  lpfS += (pPot[P_GENERAL][3] - lpfS) * 0.15f;
  corteBajoS += (pPot[P_BAJO][1] - corteBajoS) * 0.15f;
  filtDrumS += (pPot[P_DRUM][1] - filtDrumS) * 0.15f;
#if MEDIR
  densS = 1.05f;                                   // al medir, siempre el peor caso
#else
  densS += (pPot[P_DRUM][3] * 1.05f - densS) * 0.25f;
#endif
  largoDrum = 0.5f + pPot[P_DRUM][2];              // ×0.5 … ×1.5 en las caídas de la batería
  incAtkBajo = 1.0f / (ATK_BAJO * SR);
  { float tau = BAJO_DEC_MIN * powf(BAJO_DEC_RANGO, pPot[P_BAJO][3]) * TBAJO[tBajo].decMul; dBajo = expf(-1.0f / (tau * SR)); dBajoF = expf(-1.0f / (tau * TBAJO[tBajo].fenvRel * SR)); }
  incAtkMel = 1.0f / (ATK_MEL * SR);
  { const float r = pPot[P_MEL][2]; qMelMul = 0.45f + 3.5f * r * r; compMel = 1.0f / powf(qMelMul, 0.30f); }   // POT3 = resonancia
  decMel = MEL_DEC_MIN * powf(MEL_DEC_RANGO, pPot[P_MEL][3]);
  filtMel = exp2f((pPot[P_MEL][1] - 0.5f) * 4.0f);   // ×0.25 … ×4 sobre el reposo del filtro de cada nota
  {                                                // SAMPLES: pitch con zona muerta al centro (= como está)
    const float pp = pPot[P_SAMPLES][1];
    const float st = pp < 0.47f ? -24.0f * (0.47f - pp) / 0.47f : (pp > 0.53f ? 12.0f * (pp - 0.53f) / 0.47f : 0.0f);
    pasoMuestra = exp2f(st / 12.0f);
    revMuestraS = MUESTRA_REV_MAX * pPot[P_SAMPLES][2];
    ecoMuestraS = MUESTRA_ECO_MAX * pPot[P_SAMPLES][3] * pPot[P_SAMPLES][3];
    gMuestraB += (ganVol(pPot[P_SAMPLES][0]) - gMuestraB) * 0.25f;
  }
  oscFiltS += (pPot[P_OSC][1] - oscFiltS) * 0.15f; oscLfoS += (pPot[P_OSC][2] - oscLfoS) * 0.15f; oscVolS += (pPot[P_OSC][3] - oscVolS) * 0.15f;
  const float corte = 55.0f * powf(30.0f, corteBajoS);   // 55 Hz – 1.6 kHz
  // POT3 = RESONANCIA del bajo; igual baja cuando el filtro se abre (nunca un pico chillón arriba)
  const float rB = pPot[P_BAJO][2];
  const float q = fminf(8.0f, fmaxf(0.5f, (0.6f + 6.0f * rB * rB) * TBAJO[tBajo].qMul * (1.0f - 0.45f * corteBajoS)));
  const float comp = 1.0f / powf(q, 0.30f);

  // Ganancias de cada instrumento: volumen del panel × su play/stop "falso" (con fundido)
  const float g0[5] = { gDrumS, gKickS, gBajoS, gMelS, gOscS };
  const float gDrumObj = C.drumOn ? ganVol(pPot[P_DRUM][0]) : 0.0f;
  const float gKickObj = C.kick ? 1.0f : 0.0f;
  const float gBajoObj = C.bajoOn ? ganVol(pPot[P_BAJO][0]) : 0.0f;
  const float gMelObj  = C.melOn  ? ganVol(pPot[P_MEL][0])  : 0.0f;
  gDrumS += (gDrumObj - gDrumS) * 0.25f; gKickS += (gKickObj - gKickS) * 0.25f;
  gBajoS += (gBajoObj - gBajoS) * 0.25f; gMelS += (gMelObj - gMelS) * 0.25f;
  gOscS = 1.0f;
  envioMelBloque = gMelS;
  float v0 = volS; volS += (pPot[P_GENERAL][0] * pPot[P_GENERAL][0] - volS) * 0.15f;

  // Oscilador: POT1 → grado de la escala del estilo (siempre en tono) + octava del BTN3
  {
    const float x = pPot[P_OSC][0] * OSC_NOTAS;
    if (x < gradoPot - OSC_HISTERESIS || x > gradoPot + 1.0f + OSC_HISTERESIS) {
      gradoPot = (int)x; if (gradoPot > OSC_NOTAS - 1) gradoPot = OSC_NOTAS - 1; if (gradoPot < 0) gradoPot = 0;
    }
    const int oct = C.octOsc == 1 ? 12 : (C.octOsc == 2 ? -12 : 0);
    const int nota = notaGrado(TONICA_OSC + transp + oct, OSC_GRADO_BAJO + gradoPot);
    bool on = C.oscOn && estadoSub != E_ESPERA;
    if (nota != notaOscVista || on != oscOnVisto) {
      bool cambioNota = nota != notaOscVista;
      notaOscVista = nota; oscOnVisto = on;
      float inc = hz((float)nota) / SR;
      if (on) {
        if (!osc.vivo || osc.amp < 1e-3f) { osc.inc = inc; osc.vivo = true; }   // desde el silencio: sin portamento
        osc.incObj = inc; osc.obj = 1.0f; notaOscPub = nota;
        if (cambioNota || osc.amp < 0.5f) publicarGolpe(EV_OSC, 0.8f);
      } else { osc.obj = 0.0f; osc.incObj = inc; notaOscPub = -1; }
    }
  }

  // Filtros de bus (coeficientes una vez por bloque; el valor ya viene suavizado)
  coefFiltro(fDrumL, corteBus(filtDrumS), 0.75f); fDrumR.a1 = fDrumL.a1; fDrumR.a2 = fDrumL.a2; fDrumR.a3 = fDrumL.a3; fDrumR.k = fDrumL.k;
  coefFiltro(fMasL, corteBus(lpfS), 0.75f);       fMasR.a1 = fMasL.a1; fMasR.a2 = fMasL.a2; fMasR.a3 = fMasL.a3; fMasR.k = fMasL.k;
  const float abDrum = abiertoBus(filtDrumS), abMas = abiertoBus(lpfS);

  for (int i = 0; i < BUFFER_SAMPLES; i++) {
    bufKick[i] = 0.0f; bufBajo[i] = 0.0f; bufIndL[i] = 0.0f; bufIndR[i] = 0.0f;
    bufMelL[i] = 0.0f; bufMelR[i] = 0.0f; bufFxL[i] = 0.0f; bufFxR[i] = 0.0f;
  }

  // ── Síntesis por tramos: los pasos caen en la muestra exacta. SWING: la semicorchea par dura
  //    2·r y la impar 2·(1−r) (r = 50 % recto … 70 %); el par siempre suma dos, el pulso no se mueve.
  int hecho = 0;
  while (hecho < BUFFER_SAMPLES) {
    int m2 = BUFFER_SAMPLES - hecho;
    if (aTocando && estadoSub != E_ESPERA) {
      if (faltan <= 0.0f) {
        uint32_t cp0 = CICLOS(); avanzarPaso();
        faltan += largoPaso;
        medSec[5] += (CICLOS() - cp0) >> 6;
      }
      int hasta = (int)ceilf(faltan); if (hasta < 1) hasta = 1;
      if (hasta < m2) m2 = hasta;
      for (int t = 0; t < NUM_IND; t++) {          // REDOBLES: cada golpe en su muestra exacta
        Redoble &rd = redobles[t]; if (rd.quedan <= 0) continue;
        if (rd.falta <= 0.0f) {
          if (C.drumOn) golpeInd(t, rd.vel, false, 0.7f);
          rd.vel *= 0.85f; rd.quedan--; rd.falta += rd.cada;
          if (rd.quedan <= 0) continue;
        }
        int h = (int)ceilf(rd.falta); if (h < 1) h = 1;
        if (h < m2) m2 = h;
      }
      faltan -= (float)m2;
      for (int t = 0; t < NUM_IND; t++) if (redobles[t].quedan > 0) redobles[t].falta -= (float)m2;
    }
    faltanTramo = faltan + (float)m2;
    renderTramo(hecho, m2, corte, q, comp);
    hecho += m2;
  }
  envBajoPub = bajo.env;
  { float pulso = (float)(pasoTotal & 3) + 1.0f - faltan / sps; fasePub = pulso * 0.25f; }

  // ── Master ──
  //   batería = (bombo + 6 pistas) → su pasa-bajos · bajo · melodías · eco · oscilador · subida
  //   → pasa-bajos general → DC → limitador con lookahead → 13 kHz → techo suave
  uint32_t cm0 = CICLOS();
  const float objF = aTocando ? 1.0f : 0.0f;
  const float inv = 1.0f / BUFFER_SAMPLES;
  float pkD = 0, pkB = 0, pkM = 0, pkO = 0, pkT = 0;
  for (int n = 0; n < BUFFER_SAMPLES; n++) {
    float x = (float)n * inv;
    float gD = g0[0] + (gDrumS - g0[0]) * x, gK = g0[1] + (gKickS - g0[1]) * x, gB = g0[2] + (gBajoS - g0[2]) * x, gM = g0[3] + (gMelS - g0[3]) * x;
    float vv = v0 + (volS - v0) * x;
    fundido += (objF - fundido) * 0.003f;          // ~8 ms
    corteGate += ((estadoSub == E_ESPERA ? 0.0f : 1.0f) - corteGate) * 0.02f;
    // batería con su filtro
    float k = bufKick[n] * gK;
    float dl = k + bufIndL[n], dr = k + bufIndR[n], lp, bp;
    if (abDrum < 0.999f) {                         // abierto del todo no filtra: se salta (medido: picos de 103 % en la placa)
      pasoFiltro(fDrumL, dl + 1e-18f, lp, bp); dl += (lp - dl) * (1.0f - abDrum);
      pasoFiltro(fDrumR, dr + 1e-18f, lp, bp); dr += (lp - dr) * (1.0f - abDrum);
    }
    dl *= gD; dr *= gD;
    float ba = bufBajo[n] * gB, ml = bufMelL[n] * gM, mr = bufMelR[n] * gM;
    pkD = fmaxf(pkD, fabsf(dl)); pkB = fmaxf(pkB, fabsf(ba)); pkM = fmaxf(pkM, fabsf(ml) + fabsf(mr)); pkO = fmaxf(pkO, fabsf(oscL[n]));
    float g = vv * fundido * corteGate * MASTER;
    const float gS = vv * MASTER;                    // samples, eco y reverb: suenan (y se apagan solos) con la música detenida
    float l = (dl + ba + ml + oscL[n] + bufFxL[n]) * g + (ecoBufL[n] + smpL[n]) * gS;
    float r = (dr + ba + mr + oscR[n] + bufFxR[n]) * g + (ecoBufR[n] + smpR[n]) * gS;
    if (abMas < 0.999f) {                          // pasa-bajos general (abierto del todo, se salta)
      pasoFiltro(fMasL, l + 1e-18f, lp, bp); l += (lp - l) * (1.0f - abMas);
      pasoFiltro(fMasR, r + 1e-18f, lp, bp); r += (lp - r) * (1.0f - abMas);
    }
    l += golpeL[n] * gS; r += golpeR[n] * gS;
    float yl = l - dcX1L + 0.9985f * dcY1L; dcX1L = l; dcY1L = yl; l = yl;
    float yr = r - dcX1R + 0.9985f * dcY1R; dcX1R = r; dcY1R = yr; r = yr;
    float pk = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
    if (pk > medPico) medPico = pk;
    if (pk > pkT) pkT = pk;
    if (pk > limEnv) limEnv = pk; else limEnv = pk + (limEnv - pk) * limRel;
    float limObj = (limEnv > TECHO) ? (TECHO / limEnv) : 1.0f;
    limGain += (limObj - limGain) * 0.020f;        // ganancia suavizada: un escalón por muestra es un clic
    float sl = limDlyL[limDlyIdx], sr2 = limDlyR[limDlyIdx];
    limDlyL[limDlyIdx] = l; limDlyR[limDlyIdx] = r;
    limDlyIdx++; if (limDlyIdx >= LIM_LOOK) limDlyIdx = 0;
    if (limGain < medLimMin) medLimMin = limGain;
    l = pasoBQ(techoL, sl * limGain); r = pasoBQ(techoR, sr2 * limGain);
    if (fabsf(l) > 0.95f || fabsf(r) > 0.95f) medTecho = medTecho + 1;
    l = techoSuave(l); r = techoSuave(r);
    int32_t li = (int32_t)(l * 32000.0f), ri = (int32_t)(r * 32000.0f);
    salida[2 * n]     = (int16_t)(li > 32767 ? 32767 : li < -32768 ? -32768 : li);
    salida[2 * n + 1] = (int16_t)(ri > 32767 ? 32767 : ri < -32768 ? -32768 : ri);
  }
  nivelPub[0] = pkD; nivelPub[1] = pkB; nivelPub[2] = pkM * 0.5f; nivelPub[3] = pkO; nivelPub[4] = pkT;
  muestraPub = muestraGlobal;
  medSec[4] += (CICLOS() - cm0) >> 6;
}

// ==============================================================================================================================================
// MATRIZ 32×8 (tarea de LEDs, core 0) — 8 escenas psicodélicas amarradas al tempo (las del v1)
// ==============================================================================================================================================
const uint8_t HUE_EV[NUM_EV] = { 130, 45, 160, 0, 110, 20, 200, 24, 4, 180, 230, 192 };   // HAT CAJA PERC CLAP ABIERTO TOM MEL BOMBO BAJO ARP OSC SAMPLE

inline int xy(int x, int y) {
  if (x < 0 || x >= MATRIZ_ANCHO || y < 0 || y >= MATRIZ_ALTO) return -1;
  if (MATRIZ_ESPEJO_X) x = MATRIZ_ANCHO - 1 - x;
  if (MATRIZ_ESPEJO_Y) y = MATRIZ_ALTO - 1 - y;
  int i;
  if (MATRIZ_POR_COLUMNAS) { int yy = (MATRIZ_SERPENTINA && (x & 1)) ? MATRIZ_ALTO - 1 - y : y; i = x * MATRIZ_ALTO + yy; }
  else                     { int xx = (MATRIZ_SERPENTINA && (y & 1)) ? MATRIZ_ANCHO - 1 - x : x; i = y * MATRIZ_ANCHO + xx; }
  return STATUS_LEDS + i;
}
inline void pix(int x, int y, const CRGB &c) { int i = xy(x, y); if (i >= 0) leds[i] += c; }
void puntoSuave(float x, float y, const CRGB &c) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - x0, fy = y - y0;
  pix(x0,     y0,     c.scale8((uint8_t)((1 - fx) * (1 - fy) * 255)));
  pix(x0 + 1, y0,     c.scale8((uint8_t)(fx * (1 - fy) * 255)));
  pix(x0,     y0 + 1, c.scale8((uint8_t)((1 - fx) * fy * 255)));
  pix(x0 + 1, y0 + 1, c.scale8((uint8_t)(fx * fy * 255)));
}
inline void borrarMatriz() { for (int i = STATUS_LEDS; i < NUM_LEDS; i++) leds[i] = CRGB(0, 0, 0); }
inline void fundirMatriz(uint8_t c) { fadeToBlackBy(leds + STATUS_LEDS, MATRIZ_LEDS, c); }

float gananciaMatriz = 1.0f;
void limitarCorrienteMatriz() {
  uint32_t suma = 0;
  for (int i = STATUS_LEDS; i < NUM_LEDS; i++) suma += leds[i].r + leds[i].g + leds[i].b;
  float carga = (float)suma / ((float)MATRIZ_LEDS * 3.0f * 255.0f);
  float objetivo = (carga > CARGA_MAXIMA) ? (CARGA_MAXIMA / carga) : 1.0f;
  if (objetivo < gananciaMatriz) gananciaMatriz = objetivo;                 // ataque: al tiro
  else gananciaMatriz += (objetivo - gananciaMatriz) * RECUPERA_LED;        // recuperación: lenta
  if (gananciaMatriz < 0.999f) {
    uint8_t g = (uint8_t)(gananciaMatriz * 255.0f);
    for (int i = STATUS_LEDS; i < NUM_LEDS; i++) leds[i].nscale8(g);
  }
}

#define NUM_ESCENAS 11
#define E_TUNEL     0
#define E_PLASMA    1
#define E_CALEIDO   2
#define E_ESPIRAL   3
#define E_FIGURAS   4
#define E_MOIRE     5
#define E_ONDAS     6
#define E_DAMERO    7
#define E_INTERF    8
#define E_CUADROS   9
#define E_RAYOS    10
int      escena = E_TUNEL;
uint32_t vistoGolpe[NUM_EV];
float    flash[NUM_EV];
bool     nuevo[NUM_EV];
#define MAX_FIGURAS 24
Figura   figuras[MAX_FIGURAS];
uint32_t rngLed = 0xA5A5A5A5u;
static inline float lrnd() { rngLed ^= rngLed << 13; rngLed ^= rngLed >> 17; rngLed ^= rngLed << 5; return (rngLed >> 8) * (1.0f / 16777216.0f); }

float    T = 0.0f;                    // tiempo musical, en negras
float    fk = 0.0f, eb = 0.0f;        // golpe del bombo · envolvente del bajo
float    brilloEsc = 1.0f;
uint32_t bombosVistos = 0;
uint8_t hueBase = 0; float hueSpan = 120.0f, dirE = 1.0f, paramE = 0.5f;

static inline float fsen(float x) { float p = x * 0.15915494f; p -= floorf(p); return seno(p); }
static inline float fcos(float x) { return fsen(x + 1.5707963f); }
static inline float frac(float x) { return x - floorf(x); }
inline void pon(int x, int y, float hue, float v, uint8_t sat = 255) {
  if (v <= 0.004f) return;
  if (v > 1.0f) v = 1.0f;
  pix(x, y, CHSV((uint8_t)(hueBase + (int)hue), sat, (uint8_t)(v * 255.0f * brilloEsc)));
}

void escenaTunel() {
  borrarMatriz();
  const float k = 0.30f + 0.25f * paramE, w = 0.28f + 0.35f * fk;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float dx = x - 15.5f, dy = y - 3.5f, d = sqrtf(dx * dx + dy * dy);
    float f = d * k - T * dirE, s = frac(f);
    float v = s < w ? 1.0f - s / w : 0.0f;
    pon(x, y, floorf(f) * hueSpan * 0.25f + d * 3.0f, v);
  }
}
void escenaPlasma() {
  borrarMatriz();
  const float t = T * 0.8f, esc = 0.9f + 0.7f * eb + 0.4f * paramE;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float dx = x - 15.5f, dy = y - 3.5f, d = sqrtf(dx * dx + dy * dy);
    float val = fsen(x * 0.32f + t) + fsen(y * 0.9f - t * 1.3f) + fsen((x + y) * 0.21f + t * 0.6f) + fsen(d * 0.55f - t * 1.7f);
    float c = val * esc + t * 0.5f * dirE, s = frac(c);
    float v = 1.0f - fabsf(s - 0.5f) * 2.0f; v = v * v * v;
    pon(x, y, floorf(c) * hueSpan * 0.3f + val * 20.0f, v * (1.0f + fk));
  }
}
void escenaCaleido() {
  borrarMatriz();
  const float a = T * 0.5f * dirE, cr = fcos(a), sr = fsen(a), umbral = 0.25f - 0.35f * fk;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float ax = fabsf(x - 15.5f), ay = fabsf(y - 3.5f);
    float u = fmodf(ax, 8.0f); if (u > 4.0f) u = 8.0f - u;
    float px = u * cr - ay * sr, py = u * sr + ay * cr;
    float val = fsen(px * (1.1f + paramE) + T) * fsen(py * 1.3f - T * 0.7f) + 0.5f * fsen((px + py) * 0.9f + T * 1.6f);
    float v = (val - umbral) * 2.0f;
    if (v > 0.0f) pon(x, y, ax * 6.0f + val * 80.0f + T * 12.0f, v);
  }
}
void escenaEspiral() {
  borrarMatriz();
  const float brazos = 2.0f + floorf(paramE * 3.99f), w = 0.35f + 0.30f * fk;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float dx = (x - 15.5f) * 0.35f, dy = y - 3.5f;
    float a = atan2f(dy, dx) * 0.15915494f, r = sqrtf(dx * dx + dy * dy);
    float s = frac(a * brazos + r * (0.5f + paramE * 0.6f) - T * dirE * 0.5f);
    float v = s < w ? 1.0f - fabsf(s / w * 2.0f - 1.0f) : 0.0f;
    pon(x, y, r * 25.0f + a * hueSpan + T * 10.0f, v);
  }
}
void soltarFigura(float cx, float cy, float vr, uint8_t tipo, uint8_t hue, float brillo) {
  int k = 0; float menor = 9.0f;
  for (int i = 0; i < MAX_FIGURAS; i++) { if (!figuras[i].viva) { k = i; break; } if (figuras[i].brillo < menor) { menor = figuras[i].brillo; k = i; } }
  Figura &f = figuras[k];
  f.viva = true; f.cx = cx; f.cy = cy; f.r = 0.0f; f.vr = vr; f.tipo = tipo; f.hue = hue; f.brillo = brillo;
}
void escenaFiguras() {
  fundirMatriz(70);
  const uint8_t g = hueBase;
  if (nuevo[EV_BOMBO])  soltarFigura(15.5f, 3.5f, 0.9f, 0, HUE_EV[EV_BOMBO] + g, 1.0f);
  if (nuevo[I_CAJA])    soltarFigura(4.0f + lrnd() * 24.0f, 3.5f, 0.6f, 1, HUE_EV[I_CAJA] + g, 0.9f);
  if (nuevo[I_HAT])     soltarFigura(lrnd() * 31.0f, lrnd() * 7.0f, 0.45f, 2, HUE_EV[I_HAT] + g, 0.6f);
  if (nuevo[I_PERC])    soltarFigura(4.0f + lrnd() * 24.0f, 7.0f, 0.7f, 3, HUE_EV[I_PERC] + g, 0.9f);
  if (nuevo[I_CLAP])    soltarFigura(15.5f, 3.5f, 0.75f, 2, HUE_EV[I_CLAP] + g, 0.8f);
  if (nuevo[I_ABIERTO]) soltarFigura(lrnd() * 31.0f, 0.0f, 0.5f, 2, HUE_EV[I_ABIERTO] + g, 0.5f);
  if (nuevo[I_TOM])     soltarFigura(4.0f + lrnd() * 24.0f, 7.0f, 0.55f, 0, HUE_EV[I_TOM] + g, 0.8f);
  if (nuevo[EV_ARP])    soltarFigura(lrnd() * 31.0f, lrnd() * 7.0f, 0.35f, 2, HUE_EV[EV_ARP] + g, 0.35f);
  if (nuevo[EV_MUESTRA]) soltarFigura(15.5f, 3.5f, 1.3f, 1, HUE_EV[EV_MUESTRA] + g, 1.0f);
  if (nuevo[EV_OSC])    soltarFigura((float)(notaOscPub - tonicaPub + 12) * (31.0f / 36.0f), 3.5f, 0.3f, 1, HUE_EV[EV_OSC] + g, 0.7f);
  if (nuevo[EV_KEYS]) {                            // la melodía se ve: la altura de la nota es la posición
    float x = (float)(notaKeysPub - (KEYS_DESDE - 16)) * (31.0f / 40.0f);
    soltarFigura(x, 3.5f, 0.45f, 0, HUE_EV[EV_KEYS] + g, 0.9f);
  }
  for (int i = 0; i < MAX_FIGURAS; i++) {
    Figura &f = figuras[i]; if (!f.viva) continue;
    for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
      float dx = x - f.cx, dy = y - f.cy, d;
      switch (f.tipo) {
        case 0:  d = sqrtf(dx * dx + dy * dy); break;
        case 1:  d = fmaxf(fabsf(dx), fabsf(dy)); break;
        case 2:  d = fabsf(dx) + fabsf(dy); break;
        default: d = fmaxf(fabsf(dx) * 0.866f + dy * 0.5f, -dy); break;
      }
      float v = 1.0f - fabsf(d - f.r) * 1.2f;
      if (v > 0.0f) pix(x, y, CHSV(f.hue + (uint8_t)(f.r * 6.0f), 240, (uint8_t)(v * f.brillo * 255.0f * brilloEsc)));
    }
    f.r += f.vr * (1.0f + 0.6f * fk); f.brillo *= 0.95f;
    if (f.r > 34.0f || f.brillo < 0.04f) f.viva = false;
  }
}
void escenaMoire() {
  borrarMatriz();
  const float c1x = 15.5f + 12.0f * fsen(T * 0.37f), c1y = 3.5f + 3.0f * fsen(T * 0.53f + 1.0f);
  const float c2x = 15.5f + 12.0f * fsen(T * 0.29f + 2.5f), c2y = 3.5f + 3.0f * fsen(T * 0.41f + 4.0f);
  const float k = 0.6f + 0.5f * paramE;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float d1 = sqrtf((x - c1x) * (x - c1x) + (y - c1y) * (y - c1y));
    float d2 = sqrtf((x - c2x) * (x - c2x) + (y - c2y) * (y - c2y));
    int i1 = (int)floorf(d1 * k - T * dirE), i2 = (int)floorf(d2 * k + T * dirE * 0.5f);
    if ((i1 + i2) & 1) pon(x, y, i1 * hueSpan * 0.2f + i2 * 12.0f, 0.75f + 0.25f * fk);
  }
}
void escenaOndas() {
  fundirMatriz(110);
  const float act[4] = { eb, fmaxf(fmaxf(flash[I_CAJA], flash[I_PERC]), flash[I_CLAP]), fmaxf(flash[I_HAT], flash[I_ABIERTO]), fmaxf(fmaxf(flash[EV_KEYS], flash[I_TOM]), flash[EV_OSC]) };
  for (int k = 0; k < 4; k++) {
    float A = 0.8f + 2.6f * act[k] + 0.8f * fk;
    float fr = 0.22f + 0.11f * k + 0.10f * paramE, vel = (1.0f + 0.5f * k) * dirE * 1.5f;
    for (int j = 0; j < MATRIZ_ANCHO * 2; j++) {
      float x = j * 0.5f, y = 3.5f + A * fsen(x * fr + T * vel + k * 1.7f);
      if (y < 0.0f) y = 0.0f; if (y > 7.0f) y = 7.0f;
      puntoSuave(x, y, CHSV((uint8_t)(hueBase + (int)(k * hueSpan * 0.25f + x * 2.0f)), 255, (uint8_t)(130 * brilloEsc)));
    }
  }
}
void escenaDamero() {
  borrarMatriz();
  const float lado = 2.0f + floorf(paramE * 2.99f);
  const int inv = bombosVistos & 1;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float u = x + 2.2f * fsen(y * 0.9f + T) + T * dirE * 2.0f, v = y + 1.4f * fsen(x * 0.35f - T * 0.8f);
    int cu = (int)floorf(u / lado), cv = (int)floorf(v / lado);
    if (((cu + cv) & 1) ^ inv) pon(x, y, cu * hueSpan * 0.1f + cv * 20.0f, 0.8f + 0.2f * fk);
  }
}
// Las tres escenas hipnóticas nuevas: bandas de color PLANO con negro entre ellas (se leen definidas) y
// que se mueven A SALTOS en el pulso, no deslizándose (lo que gustó de triphop_viaje).
// INTERFERENCIA: dos fuentes de ondas que saltan de lugar cada corchea; las elipses e hipérbolas que
// forman son las bandas. El bombo las engorda.
void escenaInterferencia() {
  borrarMatriz();
  const float sp = floorf(T * 2.0f) * 0.5f;
  const float ax = 15.5f + 11.0f * fsen(sp * 0.9f), ay = 3.5f + 3.0f * fsen(sp * 1.3f + 1.0f);
  const float bx = 15.5f - 11.0f * fsen(sp * 0.7f + 2.0f), by = 3.5f - 3.0f * fsen(sp * 1.1f);
  const float k = 0.45f + 0.45f * paramE, grueso = 0.30f + 0.35f * fk;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float d1 = sqrtf((x - ax) * (x - ax) + (y - ay) * (y - ay)), d2 = sqrtf((x - bx) * (x - bx) + (y - by) * (y - by));
    float v = (d1 + d2 * dirE) * k - floorf(T * 4.0f) * 0.25f;
    float b = floorf(v), f = v - b;
    if (f < grueso) pon(x, y, ((int)b & 3) * hueSpan * 0.25f, 1.0f);
  }
}
// CUADRADOS: anillos cuadrados (o rombos) que nacen del centro, un anillo nuevo por semicorchea.
void escenaCuadrados() {
  borrarMatriz();
  const float t = floorf(T * 4.0f) * 0.5f * dirE, ax = 0.45f + 0.35f * paramE;
  const bool rombo = paramE > 0.5f;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float dx = fabsf(x - 15.5f) * ax, dy = fabsf(y - 3.5f);
    float d = rombo ? dx + dy : fmaxf(dx, dy);
    int r = (int)floorf(d - t);
    if ((r & 1) == 0) pon(x, y, (r & 7) * hueSpan * 0.12f, 0.80f + 0.20f * fk);
  }
}
// RAYOS: rayos desde el centro que giran un escalón por negra; el bombo los ensancha.
void escenaRayos() {
  borrarMatriz();
  const float nr = 3.0f + floorf(paramE * 4.99f);
  const float giro = floorf(T) * dirE / (nr * 2.0f), ancho = 0.28f + 0.30f * fk;
  for (int x = 0; x < MATRIZ_ANCHO; x++) for (int y = 0; y < MATRIZ_ALTO; y++) {
    float dx = (x - 15.5f) * 0.40f, dy = y - 3.5f;
    float a = atan2f(dy, dx) * 0.15915494f + giro, r = sqrtf(dx * dx + dy * dy);
    float u = a * nr, s2 = u - floorf(u);
    if (s2 < ancho) pon(x, y, floorf(u) * hueSpan * 0.15f + r * 10.0f, 1.0f - r * 0.05f);
  }
}
void pruebaOrientacion(unsigned long t) {
  borrarMatriz();
  pix(0, 0, CRGB(120, 0, 0));                                  // arriba-izquierda: ROJO
  pix(MATRIZ_ANCHO - 1, 0, CRGB(0, 120, 0));                   // arriba-derecha: VERDE
  pix(0, MATRIZ_ALTO - 1, CRGB(0, 0, 120));                    // abajo-izquierda: AZUL
  int x = (int)((t % 1500) * MATRIZ_ANCHO / 1500);             // columna que barre de izquierda a derecha
  for (int y = 0; y < MATRIZ_ALTO; y++) pix(x, y, CRGB(50, 50, 50));
}

// ── LEDs de la placa ──
float pulsoNegra = 0.0f;
const uint8_t HUE_PANEL[NUM_PANELES] = { 96, 0, 32, 128, 160, 200 };   // verde · rojo · ámbar · cian · azul · violeta
volatile uint32_t panelCambioMs = 0;
void ledsEstado(bool corre) {
  const float f[5] = { flash[EV_BOMBO], flash[EV_BAJO], fmaxf(flash[I_HAT], flash[I_ABIERTO]),
                       fmaxf(fmaxf(flash[I_CAJA], flash[I_PERC]), fmaxf(flash[I_CLAP], flash[I_TOM])), fmaxf(fmaxf(flash[EV_KEYS], flash[EV_MUESTRA]), fmaxf(flash[EV_ARP], flash[EV_OSC])) };
  const uint8_t hue[5] = { HUE_EV[EV_BOMBO], HUE_EV[EV_BAJO], HUE_EV[I_HAT], HUE_EV[I_PERC], HUE_EV[EV_KEYS] };
  for (int i = 0; i < 5; i++) leds[i] = CHSV(hue[i], 220, (uint8_t)(8 + 140 * f[i]));
  const bool mute[5] = { !reqC.drumOn || !reqC.kick, !reqC.bajoOn, !reqC.drumOn, !reqC.drumOn, !reqC.melOn };   // callado: rojo tenue
  for (int i = 0; i < 5; i++) if (mute[i]) leds[i] = CRGB(28, 0, 0);
  if (breakPub) { leds[0] = CRGB(90, 0, 0); leds[1] = CRGB(90, 0, 0); }
  if (estadoSubPub == E_SUBIDA && ((millis() / 300) & 1)) { leds[0] = CRGB(0, 0, 0); leds[1] = CRGB(0, 0, 0); }
  if (estadoSubPub == E_ESPERA) { leds[0] = CRGB(200, 0, 0); leds[1] = CRGB(200, 0, 0); }
  const int p = panelPub;
  leds[5] = CHSV(HUE_PANEL[p], 230, corre ? (uint8_t)(40 + 150 * pulsoNegra) : 40);
  if (millis() - panelCambioMs < 350) for (int i = 0; i < 6; i++) leds[i] = CHSV(HUE_PANEL[p], 230, 170);   // recién cambiado: todos del color del panel
  if (millis() < cargaAltaHasta) leds[5] = CRGB(140, 0, 140);
}

uint32_t vistoContador = 0, bloqueEscena = 0;
bool estabaTocando = false; int estiloVisto = -1;
void elegirEscena() {
  int n = (int)(lrnd() * (NUM_ESCENAS - 1)); escena = (escena + 1 + n) % NUM_ESCENAS;   // nunca la misma
  hueBase = (uint8_t)(ESTILOS[reqC.estilo].hue + lrnd() * 60.0f);                      // la paleta parte del color del estilo
  hueSpan = 40.0f + lrnd() * 215.0f;
  dirE    = lrnd() < 0.5f ? -1.0f : 1.0f;
  paramE  = lrnd();
  for (int i = 0; i < MAX_FIGURAS; i++) figuras[i].viva = false;
}

void renderLEDs() {
  static unsigned long ultimo = 0;
  unsigned long t = millis();
  if (t - ultimo < LED_REFRESH_MS) return;
  float dt = (ultimo == 0) ? 0.0f : (float)(t - ultimo) * 0.001f;
  if (dt > 0.1f) dt = 0.1f;
  ultimo = t;

  for (int ev = 0; ev < NUM_EV; ev++) {
    uint32_t g = golpes[ev];
    nuevo[ev] = (g != vistoGolpe[ev]); vistoGolpe[ev] = g;
    if (nuevo[ev]) flash[ev] = 0.5f + 0.5f * velGolpe[ev];
  }
  bool corre = tocandoPub;
  uint32_t cp = contadorPasos;
  if (cp != vistoContador) {
    vistoContador = cp;
    if ((pasoTotalPub & 3) == 0) pulsoNegra = 1.0f;
    if ((cp >> 6) != bloqueEscena) { bloqueEscena = cp >> 6; elegirEscena(); }   // cada 4 compases
  }
  if (corre && !estabaTocando) elegirEscena();                                   // en cada Play
  if (reqC.estilo != estiloVisto) { estiloVisto = reqC.estilo; elegirEscena(); } // y en cada estilo
  estabaTocando = corre;

  T += dt * (corre ? bpmPub / 60.0f : 0.25f);
  if (nuevo[EV_BOMBO]) { T += (roundf(T) - T) * 0.6f; bombosVistos++; }
  fk = flash[EV_BOMBO]; eb = envBajoPub;
  brilloEsc = corre ? 1.0f : 0.35f;

  int e = escena;
  if (!corre && (e == E_FIGURAS || e == E_ONDAS)) e = E_TUNEL;
  if (t < PRUEBA_MS) pruebaOrientacion(t);
  else switch (e) {
    case E_TUNEL:   escenaTunel();   break;
    case E_PLASMA:  escenaPlasma();  break;
    case E_CALEIDO: escenaCaleido(); break;
    case E_ESPIRAL: escenaEspiral(); break;
    case E_FIGURAS: escenaFiguras(); break;
    case E_MOIRE:   escenaMoire();   break;
    case E_ONDAS:   escenaOndas();   break;
    case E_DAMERO:  escenaDamero();  break;
    case E_INTERF:  escenaInterferencia(); break;
    case E_CUADROS: escenaCuadrados(); break;
    default:        escenaRayos();   break;
  }
  ledsEstado(corre);
  limitarCorrienteMatriz();
  FastLED.show();

  for (int ev = 0; ev < NUM_EV; ev++) flash[ev] *= 0.80f;
  pulsoNegra *= 0.80f;
}

// ==============================================================================================================================================
// CONTROLES (tarea de control, core 0, 1 kHz)
// ==============================================================================================================================================
bool bNivel[5] = { HIGH, HIGH, HIGH, HIGH, HIGH };
unsigned long bTiempo[5];
Ctl  ctl;                           // lo que mandan los botones
int  panel = P_GENERAL;
// BTN5: un toque entra o sale del break. Mantenido BREAK_LARGO_MS arranca la SUBIDA del v1 (16
// compases que terminan en un golpe y esperan en silencio); el próximo toque es el DROP.
const unsigned long BREAK_LARGO_MS = 2000;
unsigned long t5 = 0;
bool sosteniendo5 = false, enSubida = false;

float readPot(uint8_t pin) {
  uint32_t s = 0;
  for (int i = 0; i < 4; i++) s += analogRead(pin);   // 4 y no 8: cada analogRead del S3 cuesta decenas de µs
  return (float)(s >> 2) / 4095.0f;
}
// Pick-up: al cambiar de panel cada pot queda "suelto" hasta que lo muevas un 4 %; desde ahí su
// posición es el valor. Así cambiar de panel nunca hace saltar nada.
float potEntrada[4];
bool  potTomado[4] = { true, true, true, true };
const float UMBRAL_TOMA = 0.04f;
void soltarPots() { for (int i = 0; i < 4; i++) { potEntrada[i] = readPot(POT_PIN[i]); potTomado[i] = false; } }

void publicarCtl() { memcpy((void *)&reqC, &ctl, sizeof(ctl)); }

void aplicarEstilo(int e) {                        // el estilo trae sus patrones y sus timbres; después se cambian en cada panel
  const Estilo &E = ESTILOS[e];
  ctl.estilo = e;
  ctl.patDrum = 0; ctl.patBajo = 0; ctl.patMel = 0;
  ctl.kit = E.kit; ctl.tBajo = E.tBajo; ctl.tMel = E.tMel; ctl.tOsc = E.tOsc;
  ctl.glide = E.tBajo == TB_808;                  // el 808 se desliza entre notas
}

void cambiarPanel(int p) {
  panel = p; panelPub = p; panelCambioMs = millis();
  soltarPots();
}

// Un botón = una función, en el flanco de presión. En TODOS los paneles BTN4 pasa al siguiente
// panel y BTN5 entra o sale del break; BTN1–BTN3 son del panel.
void accionBoton(int b) {
  if (b == 3) {
    cambiarPanel((panel + 1) % NUM_PANELES);
  } else if (b == 4) {
    if (enSubida) { enSubida = false; ctl.brk = false; reqDrop = reqDrop + 1; }   // el DROP: todo desde el "1"
    else { ctl.brk = !ctl.brk; t5 = millis(); sosteniendo5 = true; }
  } else switch (panel) {
    case P_GENERAL:
      if (b == 0)      { ctl.tocar = !ctl.tocar; ctl.brk = false; enSubida = false; }
      else if (b == 1) aplicarEstilo((ctl.estilo + 1) % NUM_ESTILOS);
      else             reqFill = reqFill + 1;
      break;
    case P_BAJO:
      if (b == 0)      ctl.distBajo = !ctl.distBajo;            // distorsión sí / no
      else if (b == 1) ctl.patBajo = (ctl.patBajo + 1) % NUM_PAT;
      else             { ctl.tBajo = (ctl.tBajo + 1) % NUM_TBAJO; ctl.glide = ctl.tBajo == TB_808; }
      break;
    case P_DRUM:
      if (b == 0)      ctl.extra = (ctl.extra + 1) % (NUM_EXTRA + 1);   // percusión extra: 1 → 2 → 3 pistas → nada
      else if (b == 1) ctl.patDrum = (ctl.patDrum + 1) % NUM_PAT;
      else             ctl.kit = (ctl.kit + 1) % NUM_KITS;
      break;
    case P_MEL:
      if (b == 0)      ctl.octMel = (ctl.octMel + 1) % 3;       // octava: normal → +1 → −1
      else if (b == 1) ctl.patMel = (ctl.patMel + 1) % NUM_PAT;
      else             ctl.tMel = (ctl.tMel + 1) % NUM_TMEL;
      break;
    case P_OSC:
      if (b == 0)      ctl.oscOn = !ctl.oscOn;
      else if (b == 1) ctl.tOsc = (ctl.tOsc + 1) % NUM_TOSC;
      else             ctl.octOsc = (ctl.octOsc + 1) % 3;   // normal → +1 → −1
      break;
    case P_SAMPLES:
      reqBanco[b] = reqBanco[b] + 1;              // BTN1 golpes · BTN2 voces · BTN3 fx
      break;
  }
  publicarCtl();
}

void pasoControl() {
  unsigned long ahora = millis();
  for (int b = 0; b < 5; b++) {
    bool nivel = digitalRead(BTN_PIN[b]);
    if (nivel == LOW && bNivel[b] == HIGH && (ahora - bTiempo[b]) > DEBOUNCE_MS) {
      bTiempo[b] = ahora;
      accionBoton(b);
    }
    bNivel[b] = nivel;
  }
  if (sosteniendo5 && bNivel[4] == HIGH) sosteniendo5 = false;
  if (sosteniendo5 && ahora - t5 > BREAK_LARGO_MS) {   // mantenido: empieza la SUBIDA
    sosteniendo5 = false;
    if (ctl.tocar) { enSubida = true; ctl.brk = true; reqSubida = reqSubida + 1; publicarCtl(); }
  }

  static uint8_t scan = 0;
  int i = scan; scan = (scan + 1) & 3;
  float v = readPot(POT_PIN[i]);
  if (!potTomado[i] && fabsf(v - potEntrada[i]) > UMBRAL_TOMA) potTomado[i] = true;
  if (potTomado[i]) pPot[panel][i] += (v - pPot[panel][i]) * 0.3f;
}

// ─── USB: líneas para las proyecciones ───────────────────────
uint32_t enviados[NUM_EV];
void enviarLineas(unsigned long t) {
  char b[400];
#if LINEA_RESONANCIA
  static unsigned long ultInfo = 0; static int estiloInfo = -1;
  if (t - ultInfo > 5000 || estiloInfo != reqC.estilo) {
    ultInfo = t; estiloInfo = reqC.estilo;
    snprintf(b, sizeof(b), "I,%s,PercuSynth Matriz Oscura,%.2f,0,%d,%d,batería|bajo|melodías|oscilador\n",
             ESTILOS[reqC.estilo].nombre, (double)bpmPub, reqC.estilo + 1, NUM_ESTILOS);
    Serial.print(b);
  }
#endif
  const uint32_t pt = pasoTotalPub, ms = muestraPub / (SAMPLE_RATE / 1000);
  const float fase = fasePub;
  const uint32_t cDrum = golpes[I_HAT] + golpes[I_CAJA] + golpes[I_PERC] + golpes[I_CLAP] + golpes[I_ABIERTO] + golpes[I_TOM] + golpes[EV_BOMBO];
  const uint32_t cMel = golpes[EV_KEYS] + golpes[EV_ARP];
  const bool on[4] = { reqC.drumOn, reqC.bajoOn, reqC.melOn, reqC.oscOn };
  const int volI[4] = { (int)(pPot[P_DRUM][0] * 1000), (int)(pPot[P_BAJO][0] * 1000), (int)(pPot[P_MEL][0] * 1000), (int)(pPot[P_OSC][3] * 1000) };
  const int filI[4] = { (int)(pPot[P_DRUM][1] * 500), (int)(pPot[P_BAJO][1] * 500), (int)(pPot[P_MEL][1] * 500), (int)(pPot[P_OSC][1] * 500) };   // Resonancia: 0.5 = abierto
  int niv[5]; for (int k = 0; k < 5; k++) { float n = nivelPub[k] * 2.0f; niv[k] = (int)((n > 1 ? 1 : n) * 1000); }
  int btn = 0; for (int k = 0; k < 5; k++) if (bNivel[k] == LOW) btn |= 1 << k;
  int tom = 0; for (int k = 0; k < 4; k++) if (potTomado[k]) tom |= 1 << k;
#if LINEA_RESONANCIA
  // R: el formato de 43 campos de mezclador_pistas / trance_pistas. Pistas: 0 batería · 1 bajo · 2 melodías · 3 oscilador
  snprintf(b, sizeof(b), "R,%lu,%d,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
    (unsigned long)ms, (int)(fase * 1000), (unsigned long)(pt >> 2), tocandoPub ? 1 : 0,
    on[0] ? volI[0] : 0, on[1] ? volI[1] : 0, on[2] ? volI[2] : 0, on[3] ? volI[3] : 0, filI[0], filI[1], filI[2], filI[3],
    niv[0], niv[1], niv[2], niv[3],
    (unsigned long)cDrum, (unsigned long)golpes[EV_BAJO], (unsigned long)cMel, (unsigned long)golpes[EV_OSC],
    (unsigned long)golpes[EV_BOMBO], (unsigned long)(golpes[I_CAJA] + golpes[I_CLAP]), (unsigned long)(golpes[I_HAT] + golpes[I_ABIERTO]),
    0UL, 0, 0, 0, btn, panel, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, tom);
  Serial.print(b);
#endif
#if LINEA_PROYECCION
  // P: todo lo de cada instrumento (ver README). Los contadores sólo suben: una línea perdida no pierde golpes.
  int mute = (reqC.drumOn ? 0 : 1) | (reqC.kick ? 0 : 2) | (reqC.bajoOn ? 0 : 4) | (reqC.melOn ? 0 : 8) | (reqC.oscOn ? 0 : 16);
  snprintf(b, sizeof(b), "P,%lu,%d,%d,%d,%lu,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
    (unsigned long)ms, reqC.estilo, panel, tocandoPub ? 1 : 0, (unsigned long)pt, (int)(bpmPub * 10), acordePub, tonicaPub,
    (unsigned long)golpes[EV_BOMBO], (unsigned long)golpes[I_HAT], (unsigned long)golpes[I_CAJA], (unsigned long)golpes[I_PERC],
    (unsigned long)golpes[I_CLAP], (unsigned long)golpes[I_ABIERTO], (unsigned long)golpes[I_TOM],
    (unsigned long)golpes[EV_BAJO], (unsigned long)golpes[EV_KEYS], (unsigned long)golpes[EV_ARP], (unsigned long)golpes[EV_OSC],
    notaBajoPub, notaKeysPub, notaArpPub, notaOscPub,
    niv[0], niv[1], niv[2], niv[3], niv[4], (int)(envBajoPub * 1000),
    mute, breakPub ? (estadoSubPub == E_SUBIDA ? 2 : (estadoSubPub == E_ESPERA ? 3 : 1)) : 0, repeatPub ? 1 : 0, fillPub ? 1 : 0,
    (int)(pPot[P_GENERAL][2] * 1000), (int)(pPot[P_GENERAL][3] * 1000), (int)(pPot[P_DRUM][3] * 1000),
    reqC.kit, reqC.tBajo, reqC.tMel);
  Serial.print(b);
#endif
}

// ==============================================================================================================================================
// SETUP
// ==============================================================================================================================================
void i2s_init() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear    = true;
  chan_cfg.dma_desc_num  = 4;
  chan_cfg.dma_frame_num = BUFFER_SAMPLES;   // ≈ 12 ms de cola: los botones suenan en el acto
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCK,
      .ws   = (gpio_num_t)I2S_LCK,
      .dout = (gpio_num_t)I2S_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { false, false, false },
    },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

#ifndef SIMULADOR
void audioTask(void *);
void controlTask(void *);
void ledTask(void *);
#endif

void iniciarLEDs() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHT);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);
  FastLED.clear();
  FastLED.show();
}

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // OBLIGATORIO con audio: un print por USB CDC bloquea hasta que el PC lea
  for (int b = 0; b < 5; b++) pinMode(BTN_PIN[b], INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  prepararSintesis();
  uint32_t semilla = 0;
  for (int i = 0; i < 16; i++) semilla = semilla * 31u + (uint32_t)analogRead(POT_PIN[3]);
  rngComp ^= semilla ^ (uint32_t)micros();
  memset(&ctl, 0, sizeof(ctl));
  ctl.drumOn = ctl.bajoOn = ctl.melOn = true; ctl.kick = true; ctl.arp = true;
  aplicarEstilo(0);
  publicarCtl();
  for (int p = 0; p < 4; p++) pPot[P_GENERAL][p] = readPot(POT_PIN[p]);   // el General ya en su lugar al arrancar
  lpfS = pPot[P_GENERAL][3];

#ifdef SIMULADOR
  iniciarLEDs();
#endif

  i2s_init();
#if MEDIR
  Serial0.begin(115200);
  ctl.tocar = true; ctl.oscOn = true; ctl.extra = NUM_EXTRA; ctl.distBajo = true; publicarCtl();   // arranca tocando, con todo, para medir sin tocar nada
#endif

#ifndef SIMULADOR
  // El audio tiene el core 1 para él solo; controles, USB y matriz van al core 0.
  xTaskCreatePinnedToCore(audioTask,   "audio",   8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 6144, NULL,  3, NULL, 0);
  xTaskCreatePinnedToCore(ledTask,     "leds",    4096, NULL,  2, NULL, 0);
#endif
}

// ==============================================================================================================================================
// TAREAS
// ==============================================================================================================================================
#ifdef SIMULADOR
void loop() {
  pasoControl(); calcularBloque();
  size_t w; i2s_channel_write(tx_chan, salida, sizeof(salida), &w, portMAX_DELAY);
  renderLEDs();
}
#else
// Presupuesto de un bloque: 128 muestras a 240 MHz = 696 000 ciclos. Si un bloque usa más del
// 80 %, el LED 5 se pone magenta medio segundo: así se distingue un problema de tiempo de uno de señal.
const uint32_t CICLOS_BLOQUE = (uint32_t)(240000000.0 * BUFFER_SAMPLES / SAMPLE_RATE);
void audioTask(void *) {
  for (;;) {
    uint32_t c0 = ESP.getCycleCount();
    calcularBloque();
    uint32_t ciclos = ESP.getCycleCount() - c0;
    if (ciclos > (CICLOS_BLOQUE / 10) * 8) cargaAltaHasta = millis() + 500;
    if (ciclos > medMax) medMax = ciclos;
    medSuma = medSuma + ciclos / 64; medN = medN + 1;
    size_t w;
    i2s_channel_write(tx_chan, salida, sizeof(salida), &w, portMAX_DELAY);
  }
}
void controlTask(void *) {
  unsigned long ultLinea = 0;
  for (;;) {
    pasoControl();
    unsigned long t = millis();
    if (t - ultLinea >= LINEA_MS) { ultLinea = t; enviarLineas(t); }
#if MEDIR
    static unsigned long tMed = 0, tSmp = 0, tEst = 0;
    if (millis() - tSmp >= 1500) {                 // un sample cada 1.5 s, cambiando de banco
      static int bm = 0; tSmp = millis(); bm = (bm + 1) % NUM_BANCOS; reqBanco[bm] = reqBanco[bm] + 1;
    }
    if (millis() - tEst >= 8000) {                 // y un estilo nuevo cada 8 s
      tEst = millis(); aplicarEstilo((ctl.estilo + 1) % NUM_ESTILOS); ctl.tocar = true; ctl.oscOn = true; ctl.extra = NUM_EXTRA; ctl.distBajo = true; publicarCtl();
    }
    if (millis() - tMed >= 2000) {
      tMed = millis();
      uint32_t n = medN ? medN : 1;
      Serial0.printf("CPU audio: prom %.0f %%  max %.0f %%  | pico antes del limitador %.2f  ganancia min %.2f  muestras en techo %lu | BPM %.0f estilo %s\n",
        100.0 * (double)medSuma * 64.0 / n / CICLOS_BLOQUE, 100.0 * (double)medMax / CICLOS_BLOQUE,
        (double)medPico, (double)medLimMin, (unsigned long)medTecho, (double)bpmPub, ESTILOS[reqC.estilo].nombre);
      Serial0.printf("   por sección (%% del presupuesto): batería %.0f  bajo %.0f  melodías %.0f  oscilador %.0f  master %.0f  pasos %.0f\n",
        100.0*64*medSec[0]/n/CICLOS_BLOQUE, 100.0*64*medSec[1]/n/CICLOS_BLOQUE, 100.0*64*medSec[2]/n/CICLOS_BLOQUE,
        100.0*64*medSec[3]/n/CICLOS_BLOQUE, 100.0*64*medSec[4]/n/CICLOS_BLOQUE, 100.0*64*medSec[5]/n/CICLOS_BLOQUE);
      for (int k = 0; k < 6; k++) medSec[k] = 0;
      medMax = 0; medSuma = 0; medN = 0; medTecho = 0; medLimMin = 1.0f; medPico = 0.0f;
    }
#endif
    vTaskDelay(1);
  }
}
// Los LEDs se inicializan DENTRO de su tarea, en el core 0: las interrupciones del RMT corren en el
// núcleo que lo inicializó (hecho en setup(), que corre en el core 1, le robaba al audio hasta 30 %
// de un bloque — medido en industrial_matriz).
void ledTask(void *)     { iniciarLEDs(); for (;;) { renderLEDs(); vTaskDelay(2); } }
void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
#endif
