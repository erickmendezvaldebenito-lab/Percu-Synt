# sesion

Una sesión pequeña, sonora y resonante, pensada con las ideas de Ableton Live: **pistas por capas, todo coordinado a un reloj, escala global, escenas, envíos y cambios cuantizados**. Cuatro pistas + una pista maestra. **Pulsar un botón no suena**: el cambio queda *armado* y entra en el siguiente compás con un cruce corto, así que no hay clics ni golpes.

## Qué tomé de Live y cómo queda aquí

| En Live | Aquí |
|---|---|
| Pistas con sus mandos y botón de silencio | 4 capas + maestra; POT1 volumen, BTN2 stop/play (fundido de ≈ 30 ms) |
| Escala global (Scale Awareness) | **SESION**: una tónica y una escala; todo lo que tiene altura se calcula en grados de esa escala |
| Reloj único + rejilla por pista + Groove | Un reloj de 16 pasos por compás con **swing**; BAJO y MELODIA eligen rejilla 1/4 · 1/8 · 1/16 con POT2 |
| Cuantización de lanzamiento | Todo lo que pides se aplica **al inicio del compás**; mientras tanto la pantalla muestra ARMADO y el LED 6 parpadea violeta |
| Escenas y *follow actions* | 8 escenas (menú con BTN1 mantenido); en **AUTO** pasa sola a otra cada 16 compases |
| Retornos (reverb y delay) | Un mando de **ENVIOS**: reverb de placa y eco sincronizado (corchea con puntillo) |
| Resonators | La pista **RESONADOR**: 4 filtros de peine afinados al acorde que vibran por simpatía con lo que suena |
| Modelos físicos de cuerda | **MELODIA**: cuerdas Karplus-Strong afinadas a la escala |
| Compresión por sidechain | El bajo "respira" sobre los acordes |
| Macros | POT3 y POT4 de cada pista son sus macros |
| Compresor y Limiter en master | Auto-nivel + limitador + techo de agudos |

(Referencias de memoria sobre Live; no consulté la documentación de Ableton en esta sesión.)

## Controles

Fijos en todos mis firmwares: **BTN1 corto** = pista siguiente · **BTN1 mantenido** = menú de escenas · **POT1** volumen · **BTN2** stop/play · **POT2** tempo/rejilla · todo se guarda solo en la microSD (`/SESION/escena.txt`).
Orden de BTN1: BAJO → ACORDES → MELODIA → TEXTURA → RESONADOR → MATRIZ → SESION.

| Pista | POT2 | POT3 | POT4 | BTN3 | BTN4 | BTN5 |
|---|---|---|---|---|---|---|
| 1 BAJO | rejilla 1/4 · 1/8 · 1/16 | filtro resonante | cuerpo (sub ↔ sierra) | patrón (4x4 · tresillo · octavos · pedal · roto · sincopa · 2-step · UKG) | octava | **timbre: SUB · ACIDO (sierra, resonancia alta, saturación, glide) · REESE (dos sierras desafinadas + sub)** |
| 2 ACORDES (**tabla de ondas**) | acorde nuevo cada 1 · 2 · 4 compases | filtro (con saturación) | ataque (15 ms – 0,8 s) | progresión (6) | voicing: triada · abierto · sus2 · sus4 · séptima | ritmo: sostenido · pulsado · **stab dub** |
| 3 MELODIA | rejilla | brillo | densidad | semilla nueva | sonido: arpa · cuerda · marimba · vidrio · **campana FM** | octava |
| 4 TEXTURA (**granular**) | densidad de granos | filtro | dispersión (posición y abanico de alturas) | congelar | tamaño de grano 10 · 25 · 50 · 100 ms | armonía: unísono · acorde · octavas · quintas |
| 5 RESONADOR | decaimiento | entrada | tono | modo: acorde · quintas · octavas · escala | altura (−1/0/+1) | congelar |
| 6 MATRIZ | ritmo de los LFO (16 … 1/2 tiempos) | **cantidad** de la ruta elegida (centro = 0) | largo del MSEG (1 · 2 · 4 compases) | fuente: LFO1 · LFO2 (razón 3:2) · MSEG · ENV | destino: corte bajo · corte acordes · tabla de ondas · brillo melodía · decaimiento resonador · envíos · tono melodía (cuantizado) | forma del LFO (seno · triángulo · salto · rampa) / curva del MSEG (8) |
| 7 SESION | **tempo 70–160** | swing | envíos | escala (8) | tónica (por quintas) | **polimetría** del bajo/melodía (16/16 · 16/12 · 16/10 · 12/16 · 14/16 · 16/20 · 12/10 · 20/12) |

En MATRIZ, POT1 = profundidad global de la modulación. Menú de escenas (BTN1 mantenido): BTN3/BTN4 eligen, BTN5 lanza (al siguiente compás), BTN2 activa/desactiva AUTO.

### Escenas (inspiradas en el mundo Dekmantel)

UFO (techno experimental, bajo ácido) · BROKEN (club roto, 2-step, stabs) · DUB (reese y stabs con eco) · ACIDO · AMBIENTE · SUB (half-time, sub y campanas FM) · ARP · NIEBLA.
Criterio: la programación 2026 de Dekmantel cruza techno/electro/acid (Helena Hauff, Jeff Mills, Underground Resistance), club roto y bajo (SHERELLE, Kode9, Mala), house (Eris Drew, Ricardo Villalobos) y experimentación (James Holden & Surgeon), y su sello UFO se mueve entre techno oscuro y experimental, acid y ambiente. No copio obras de nadie: sólo tomo esas familias de sonido y de ritmo.

## Capacidades de síntesis y modulación

| Capacidad | Dónde está | Alcance real |
|---|---|---|
| Tabla de ondas | pad de ACORDES | 8 cuadros generados por código (seno → triángulo → sierra → cuadrada → sierra rica → 2 formantes → pulso), posición movida por la matriz. No carga archivos de audio |
| FM | MELODIA, sonido CAMPANA | portadora + moduladora a razón 3,5 con índice que decae |
| Granular | TEXTURA | granos de 10–100 ms de lo que suena (remuestreo propio, 0,68 s), afinados a escala/acorde |
| Filtros | BAJO, ACORDES, RESONADOR | SVF con resonancia y saturación suave proporcional a la resonancia; peines afinados en el RESONADOR |
| Matriz de modulación | MATRIZ | 4 fuentes × 7 destinos, cantidad bipolar por ruta, profundidad global; las rutas se guardan en la SD |
| MSEG | MATRIZ | 8 curvas de varias etapas sincronizadas al compás. No se dibujan: son presets |
| Expresión por nota (MPE) | ACORDES | cada nota del acorde deriva en afinación y timbre por su cuenta; por MIDI sale en su propio canal (2–5) con inflexión y CC74. Bajo en canal 6, melodía en 7 |
| Cuantización a escala | toda la altura | las notas se calculan en grados; el movimiento de tono de la matriz se cuantiza a la escala |
| Polirritmia | SESION | bajo y melodía cuentan su propio largo de patrón sobre el mismo reloj maestro |

## Robustez (medida en la placa)

* CPU del audio 45–55 % promedio, máximo 78 %, 0 retrasos.
* Prueba de estrés de 4 min (todos los botones, perillas y pistas cambiando al azar cada 0,7 s): 0 reinicios, 0 retrasos, 0 curaciones.
* Si la mezcla dejara de ser finita (NaN), se limpia todo y sigue sonando.
* Los LED se escriben con `rmtWrite` (no `FastLED.show()`, que se cuelga con la pantalla).
* `-DDIAG=1` imprime CPU, pico, escena y armados; `-DFUZZ=1` (con DIAG) lanza la prueba de estrés.

Arduino IDE: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · PSRAM: OPI · `-O2`.
