# piano_piano

Un piano muy suave que se toca solo en una escala, con un fondo industrial relajado y una capa de ambiente. **Todo comparte una sola armonía** (tónica, escala y acorde en curso), así que todo está en sintonía.

## Reglas fijas (iguales en todos los firmwares nuevos)

| Control | Función |
|---|---|
| BTN1 corto | siguiente capa |
| BTN1 mantenido 0,7 s | menú de estados guardados |
| POT1 | volumen de la capa |
| BTN2 | stop / play de la capa |
| POT2 | tempo de la capa |
| microSD | todo se guarda solo en `/PIANO_PIANO/escena.txt` (3 s después del último cambio) |

## Capas

| # | Capa | POT3 | POT4 | BTN3 | BTN4 | BTN5 |
|---|---|---|---|---|---|---|
| 1 | PIANO (POT2 = 30–120 BPM) | toque (suave ↔ brillante) | notas (densidad) | escala (8) | **sonido**: piano · fieltro · eléctrico · caja de música · vidrio · grave | estilo: melodía / arpegio / acordes / mixto |
| 2 | INDUSTRIAL (POT2 = pulso 30–90) | densidad | oscuridad | pulso | metal | **golpe** (3 metales del acorde + golpe grave, sin ruido) |
| 3 | AMBIENTE (POT2 = ritmo de la nube) | brillo del drón | nube de campanas | drón | nube | **tónica** (por quintas) |
| 4 | EFECTOS (POT1 = retorno, POT2 = BPM del eco) | reverb (tamaño y mezcla) | eco (cantidad y realimentación) | congelar reverb | color del master | eco 1/2 (corchea en vez de corchea con puntillo) |
| 5 | ESTADOS (POT1 = master, POT2 = tempo global ×0,5…×2) | tiempo de transición (2–60 s) | intensidad (macro) | estado anterior | siguiente | evolución (off / lenta / media / rápida) |

| 7 | ELECTRO (POT2 = 60–150 BPM) | filtro | mutación (0 = el patrón no cambia) | RITMO: sin · pulso · roto · denso (bombo afinado a la tónica con sidechain, hats en banda, clanks del acorde) | timbre: sierra · cuadrada · FM metal · ruido | modo: bajo · arpegio · secuencia · pulso |
| 8 | INMERSION (POT1 = cantidad, POT2 = velocidad de la órbita) | ancho estéreo | shimmer (octava arriba hacia la reverb) | órbita del campo atmosférico | shimmer | ensemble (coro) |
| 9 | MICRO (POT1 = nivel; **apagado al encender**, BTN2 lo activa) | filtro | efecto (cuánto de reverb/eco) | congelar lo capturado (0,68 s) | uso: ambiente · nube granular (afinada al acorde) · eco · sólo control | influye: el nivel del micro abre el filtro, aumenta la mutación y la reverb |

Orden de BTN1: PIANO → ELECTRO → INDUSTRIAL → AMBIENTE → INMERSION → MICRO → EFECTOS → VIAJE → ESTADOS.

**Ambiente sin fin:** al encender, EVOLUCION ya está en MEDIA (cambia de estado, de escala y a veces de tónica cada ~1 min; se apaga en ESTADOS con BTN5: off · lenta 2 min · media 1 min · rápida 25 s): cada tanto la música pasa sola a otro estado (deslizando según POT3) y a veces cambia de tónica por quintas. ELECTRO muta compás a compás con su POT4 y, si activas INFLUYE, también con el micro. Todo en una sola armonía.

**Micrófono (INMP441, I2S1 en pines 11/12/13):** sólo entra a la mezcla (reverb, eco, color, ancho de la capa INMERSION); no dispara notas. Cuidado con la realimentación acústica si el volumen del micro es alto y el micrófono oye los parlantes.

BTN2 en EFECTOS = bypass; en ESTADOS = parar / reanudar todo.

**Sin sonidos que crecen solos:** se eliminaron del instrumento (no sólo se apagaron) el vapor, el barrido/riser, las rupturas con ruido y toda la capa VIAJE. Los estados ya no mueven los volúmenes de las capas (POT1 es tuyo) y el shimmer nace apagado. Lo único autónomo es lo tonal: piano, campanas, ELECTRO y la evolución de estados.

**LEDs**: se escriben con `rmtWrite` del núcleo (no `FastLED.show()`): FastLED se colgaba 1 s (hasta 4 s) cada vez que la pantalla hacía una transacción I2C y congelaba botones y perillas.

## Por qué está en sintonía

* **Escala**: DÓRICO · LIDIO · EOLICO · MIXOLIDIO · PENTA MENOR · PENTA MAYOR · HIRAJOSHI · INSEN. Todas las alturas se calculan en grados de esa escala: nada puede quedar fuera de tono.
* **Acorde en curso**: cambia solo cada ~16 tiempos del piano (más lento = más calma). El piano lo recorre (con sesgo a notas del acorde en los tiempos fuertes), el drón lo suena, las campanas lo prefieren y los metales son sus notas.
* **Industrial afinado**: el zumbido es la tónica (con 2.º y 3.er armónico), el golpe cae en la tónica, los metales usan parciales armónicos (×1, ×2,003, ×3,007, ×5,011) sobre notas del acorde, y el vapor es ruido filtrado en un armónico de la raíz del acorde.
* **Nada salta**: cambiar de estado desliza los 16 valores (tiempo en ESTADOS · POT3); si mueves una perilla mientras se desliza, ese valor es tuyo.

## Estados

CALMA · NIEBLA · FABRICA DISTANTE · NOCHE · DERIVA · CRISTAL · MAQUINA · SUSPENSO.
En el menú (BTN1 mantenido): BTN3/BTN4 eligen, BTN5 carga, BTN2 guarda **tu mezcla actual** encima de ese estado (queda un `*`; el archivo es `/PIANO_PIANO/estadoN.txt` y se puede editar).

## Arduino IDE

ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · PSRAM: OPI · optimización `-O2` · requiere FastLED y la tarjeta microSD en FAT32.

`-DDIAG=1` imprime por Serial0 la carga de CPU, retrasos y pico (el USB de la placa es un puente UART; BTN1 queda inhabilitado en DIAG).

## Robustez (medida)

* **Sin sonidos que aparecen solos**: el drón es un pedal fijo en la tónica (sólo se reafina si tú cambias escala/tónica/tipo; antes crecía en cada cambio de acorde); se quitó el rumor de aire del zumbido; el VAPOR, la RUPTURA y los VIAJES sólo suenan cuando los pides.
* **Autocuración**: si la mezcla dejara de ser un número finito, se limpian filtros, reverb y voces y sigue sonando (antes un NaN dejaba la placa muda hasta reiniciar). La causa real encontrada con una prueba de estrés: `sqrtf` de un −8·10⁻⁸ en el viaje TURBINA, y una rampa de filtro que seguía avanzando sin viaje.
* **Regulador de carga**: si el render pasa del 84 % del presupuesto, se aligeran shimmer, ensemble, polifonía del piano, campanas y granos hasta bajar del 70 %.
* Prueba de estrés (3 min con todos los botones, perillas y capas cambiando al azar cada 0,7 s): 0 curaciones, 71 retrasos de audio, 75 % de CPU promedio. Uso normal: 50–65 %.

## Auto-ajustes (para que nada sature)

* **Micrófono**: ganancia automática (sube despacio, baja rápido; 1×–40×), compuerta sobre el piso de ruido, antipicos y limitación suave. Ya no hay ganancia fija.
* **Mezcla**: auto-nivel (compresor suave, umbral 0,5) delante del limitador; con todas las capas a tope no satura.
* **Adaptación**: en NUBE, POT4 = abanico de alturas (unísono → dos octavas, siempre en el acorde y la escala). Con INFLUYE, el nivel del micro sube también la densidad del piano y un poco el tempo de ELECTRO.

## Nada se enciende solo (regla fija)

Las fuentes de sonido (**PULSO, METAL, DRÓN, NUBE de campanas**) son *interruptores del usuario*: solo los mueven los botones de su capa.
Un ESTADO (manual o por EVOLUCIÓN) cambia el carácter (valores de perillas, escala, timbre, órbita/shimmer/ensemble…) pero **conserva
esos cuatro interruptores como los dejaste** (`FL_SW`). Antes, `flagsA = e.fl` los pisaba y la nube de campanas reaparecía sola.
Además al arrancar sin escena guardada: **solo suena el PIANO** (`runMask = 0x01`), los interruptores van apagados y **EVOLUCIÓN está en OFF**
(se enciende con B5 en la capa ESTADOS). Una escena guardada en la microSD sí restaura lo que tenías.
