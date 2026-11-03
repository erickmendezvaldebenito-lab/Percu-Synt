# atmosfera_deep

Ambiente profundo (deep / dub techno / ambient) que **muta** y se **llena de capas** a medida que subes un pot. Es una máquina de atmósfera: no se toca nota por nota, se la deja crecer. Todo corre en un solo reloj y todo está en la escala.

La referencia estética es la escena underground de sellos y festivales como Dekmantel (graves largos, acordes que respiran en el eco, pulsos suaves que aparecen de a poco). **Ninguna melodía de terceros está transcrita**: es estética, no copia.

## Controles

| Control | Función |
|---|---|
| **BTN1** | Play / Stop. Cada Play sortea una escena nueva (la primera vez tras encender repite la guardada) |
| **BTN2** | **Filtro**: apagado → pasa-bajos → pasa-altos → pasa-banda |
| **BTN3** | **Sinte**: agrega / quita una voz melódica; su timbre sigue al POT2 (SONAR · CAMPANA · CORO) |
| **BTN4** | **Delay** ping-pong en el tempo: apagado → corchea con puntillo → negra → corchea |
| **BTN5** | **Pausas** sincronizadas: nada → RESPIRA → CORTE → DUB |
| **POT1** | Volumen |
| **POT2** | **Mutación** del ambiente (0 = un acorde quieto · 1 = se mueve cada compás) y timbre del sinte |
| **POT3** | **Escala y atmósfera**: qué notas y qué capas existen |
| **POT4** | Corte del filtro (sólo suena con el filtro encendido) |

Un botón, una función, en el flanco de presión. Ningún pot cambia de significado ni se congela: la posición del knob *es* el valor.

## Cómo crece con el POT3

Cada Play sortea tonalidad (Re, Mi, Fa, Sol, La o Do) y modo (**eólico**, **dórico** o **frigio**, los más usados en este género) y un tempo de 108–124 BPM. Con el POT3 abajo sólo existen la tónica y la quinta; al subirlo entran las demás notas del modo **y** una capa nueva, para que la atmósfera crezca junto con la escala:

**Capas** (cada una entra con una rampa suave, sin saltos):

| POT3 | Capa |
|---|---|
| 0 | **DRON** (sub + 2.º armónico + quinta + octava) |
| 0,06–0,16 | **PAD** (acorde de 4 notas, sierras desafinadas, desliza entre acordes) |
| 0,20–0,30 | **TEXTURA** (ruido por dos resonadores afinados a la escala) |
| 0,34–0,44 | **PULSO** (acordes de dub en el contratiempo, con mucho eco) |
| 0,50–0,60 | **ARPEGIO** (pluck generativo) |
| 0,64–0,74 | **HATS** (ruido en banda, sin pitch) |
| 0,80–0,90 | **BOMBO** (afinado a la tónica; agacha al dron, al pad y al pulso) |

**Notas de la escala** (el pot se divide en 6 zonas con histéresis; en cada una entra una nota más):

| POT3 desde | 0 | ≈0,18 | ≈0,35 | ≈0,51 | ≈0,68 | ≈0,85 |
|---|---|---|---|---|---|---|
| Entra la… | tónica y quinta | tercera | séptima | cuarta | segunda | sexta |

Una melodía escrita con una nota que todavía no existe suena con la vecina más cercana que sí existe; al subir el pot, la nota nueva aparece justo donde el patrón la quería. Así la melodía es siempre la misma y sólo se va completando.

La segunda y la sexta son las que le dan su color a cada modo (la b2 del frigio, la 6.ª mayor del dórico): llegan al final a propósito.

## Qué hace el POT2 (mutación)

Todo llega al borde del compás y desliza, nunca salta:

- **acorde**: 0 = no cambia; subiendo cambia cada 8, 4, 2 y 1 compases, siempre por grados de la escala y sólo a raíces con tríada consonante (en el frigio el grado 5 es disminuido y no se usa). La mitad de las veces vuelve a la tónica.
- **pad**: abre y cierra su brillo; más desafinación entre sierras.
- **dron**: un batido lento entre el 2.º armónico y la octava.
- **textura**: cambia de nota.
- **arpegio**: se reescribe de a poco.
- **sinte**: SONAR (POT2 < 0,34, seno profundo con eco) · CAMPANA (0,34–0,67, FM de cristal) · CORO (> 0,67, voz de formantes). El timbre se decide al tocar cada nota.

## Pausas (BTN5)

Callan **pad, pulso, arpegio, hats, bombo y sinte**; el dron y la textura siguen y el eco y la reverb suenan en el hueco.

- **RESPIRA**: calla el 4.º tiempo de cada compás.
- **CORTE**: calla el último compás de cada frase de 4.
- **DUB**: calla tiempos sueltos (de a 4 pasos) sorteados con la semilla de la escena.

## Decisiones que tomé por ti (cámbialas si no era así)

1. **POT4 = corte del filtro.** No lo asignaste; sin una perilla el filtro no se puede tocar. Si no lo quieres, basta con una constante fija en `fcFiltro()`.
2. **POT3 hace crecer la escala y las capas a la vez** (en vez de elegir una escala entre varias). La escala (tonalidad + modo) se sortea en cada Play.
3. **Guardado en la flash interna del ESP32** (Preferences), no en la microSD. Ver abajo.
4. **BTN3 agrega un sinte cuyo timbre sigue al POT2**, en lugar de elegir entre sintes con un botón.
5. **BTN1 es sólo Play/Stop.** Las reglas fijas de 2026-10-12 (BTN1 = capa siguiente, menú en pantalla, microSD, OLED) no se aplican porque tus asignaciones las reemplazan.

## Guardado

Se guarda la escena (tonalidad, modo, tempo, semilla de los patrones) y los modos de filtro / delay / pausas / sinte. El primer Play tras encender repite la escena guardada; los siguientes sortean una nueva.

- Al **sortear una escena** (Play) y al **parar** se escribe de inmediato.
- Con la música **detenida**, un cambio de modo se guarda 3 s después de dejar de tocar.
- **No se escribe en pleno Play**: escribir en flash puede dejar al DMA sin datos y se oiría como un click. Consecuencia: un cambio de filtro/delay/pausas/sinte hecho *durante* un Play se guarda al parar; si apagas sin parar, ese cambio se pierde (la escena sí está guardada desde el Play).

## Sonido: reglas que se respetaron

- **Ruidoso ≠ distorsionado**: nada de bit-crush ni decimación. PolyBLEP en todas las sierras.
- **Voces nunca cortadas en seco**: al cruzar −48 dB la envolvente pasa a una cola de 1,5 ms y se libera a −82 dB.
- Bombo en **tres bandas** (fundamental + 2.º armónico + mazo de **ruido** en banda, nunca un seno), afinado a la tónica (41–78 Hz), una sola cabeza con fast-kill de ~3 ms. Hats en banda 8 kHz (nunca pasa-altos pelado).
- FM de la campana con modulador de razón 3,5 (siempre > 2). Saturación sólo donde hay una parcial fuerte (dron/bombo y el lazo del eco).
- **Eco**: tiempo en pasos del tempo, dos cabezas con cruce de 50 ms al cambiar (deslizar una cabeza cambia el tono); se oscurece y satura suave dentro del lazo.
- **Reverb** de 4 líneas con matriz de Householder, normalizada por su realimentación.
- Filtro maestro con resonancia atada al corte (nunca un pico resonante arriba); saturación antes del filtro; techo de 13 kHz **después** del limitador; bloqueador de continua.
- **Limitador suave sin estado** (sólo actúa sobre 0,75). Uno con lookahead costaba ~5 % de CPU y en ninguna prueba llegó a actuar.
- Audio en la tarea del **core 1**; botones, pots y LEDs en el core 0 a 1 kHz, que sólo dejan pedidos que el audio aplica en el borde de un bloque. Sin `Serial` en uso normal. Cola de DMA de ~35 ms.

## LEDs

Color = modo (violeta eólico · cian dórico · rojo frigio). LED0 dron · LED1 pad · LED2 textura/pulso · LED3 arpegio/sinte · LED4 hats/bombo · LED5 pulso del compás. Se apagan a un cuarto mientras una pausa calla la música. **LED5 magenta = el audio usa más del 80 % del CPU.**

## CPU: lo que sé y lo que no

**No está medido en la placa.** Se estimó así: se contaron las instrucciones por bloque de 128 muestras de este firmware y de tres firmwares del repo con CPU medido en la placa (`dub_capas` 52–56 %, `bruma_capas` 51–60 %, `kraftwerk_capas` 62–63 %, peor caso). Los tres dan ~3,0 ciclos por instrucción contada (±7 % entre ellos), y con ese factor:

- solo el dron (POT3 = 0): **~35 %**
- peor caso (todas las capas + sinte + filtro + delay + mutación alta): **~80 %**, con un margen de error de ±15 % que no puedo cerrar sin la placa.

Si el audio pasa del 80 % durante ~1 s, un **regulador de carga** aligera solo: nivel 1 = el pad deja de doblar sierras en raíz y quinta; nivel 2 = además calla la textura. Baja un nivel tras ~3 s por debajo del 55 %. Cada cambio llega con rampa, sin saltos.

Para medir la carga real, compila con `-DDIAG=1` (o pon `#define DIAG 1` arriba) y abre el UART de la placa (**Serial0**, 115200): cada 2 s imprime `CPU audio: prom X %  max Y % | regulador nivel N`.

## Validación hecha (en el PC, compilando el `.ino` contra mocks de Arduino/I2S/FastLED/Preferences y renderizando)

- **0 notas fuera de escala** de 13 684 (3 modos × 6 tonalidades; arpegio, pulso, sinte, pad, textura, dron y bombo). Las tríadas de los 3 modos son consonantes.
- **Reloj sin deriva**: 781 pasos en 100 s a 117 BPM.
- **Pausas** en el paso correcto, 11–15 dB más bajas, y sin escalones (el salto máximo entre muestras alrededor de las compuertas es igual o menor que en tramos sin pausas).
- **Guardado**: la escena y los modos sobreviven al reinicio; el primer Play repite la guardada; los siguientes sortean otra.
- **Nivel**: con POT1 al 80 % el pico queda en −6 dBFS; con POT1 al máximo y todo encendido, 0 muestras sobre 0,97. Sin continua y sin NaN. Agudos: 8–13 kHz queda 34 dB bajo el total y por encima de 13 kHz 46 dB bajo.
- **Regulador de carga**: sube y baja de nivel con histéresis, sin clicks.

## Lo que NO está verificado

- **Cómo suena de verdad.** No lo he escuchado. Los niveles de cada capa, el timbre de cada voz y el carácter del conjunto son una primera versión de oído por ajustar con la placa.
- Que compile con el core de Arduino y la FastLED reales (se compiló con g++ y mocks, no con el IDE).
- La carga de CPU en la placa (sólo estimada).
- Que la escritura en flash no cause ningún bache audible (se evitó escribir en pleno Play, pero no se probó en hardware).
- Los LEDs.

## Arduino IDE

ESP32S3 Dev Module · Flash Mode **DIO** · cualquier esquema de particiones · FastLED instalada. No usa PSRAM, IMU, microSD, pantalla ni MIDI.
