# productor_capas

"Productor": techno melódico en La menor, 124 BPM (108–148), con **forma de canción**. Tres capas con reloj propio y las mismas reglas que el resto de la familia (`bruma_capas`, `dub_capas`, `kraftwerk_capas`).

| Regla fija | |
|---|---|
| BTN1 | siguiente capa (BAJO → RITMO → SONIDOS) |
| POT1 | volumen de la capa |
| BTN2 | STOP / PLAY de esa capa |
| POT2 | tempo de esa capa (1 BPM por paso) |

| Capa | POT3 | POT4 | BTN3 | BTN4 | BTN5 |
|---|---|---|---|---|---|
| BAJO | corte | cuerpo | patrón (8) | **ARREGLO auto/manual** | octava |
| RITMO | punch del bombo | hats | groove (8) | relleno de toms | percusión |
| SONIDOS | voz (vocal chop) | espacio | progresión (4) | FX (riser; al soltar, impacto) | modo |

## ARREGLO AUTO

La canción se toca sola, 64 compases (~2 min a 124 BPM), con transiciones de productor:

| Sección | Compases | Suena | Transición |
|---|---|---|---|
| INTRO | 8 | bombo, hats, acordes | — |
| BUILD | 8 | + bajo, percusión | riser los últimos 2 compases, silencio de bombo y bajo en el último tiempo, redoble de toms |
| DROP | 16 | todo + arpegio/tema | impacto al soltarse el riser |
| BREAK | 8 | acordes, arpegio, vocal chop | riser los últimos 2 compases |
| DROP 2 | 16 | todo | impacto |
| OUTRO | 8 | bombo, hats, acordes | vuelve a INTRO |

La sección y el compás aparecen en la pantalla ("DROP 05/16"). En manual todo suena a la vez y armas la canción con STOP/PLAY de cada capa.

## MIDI (para Ableton)

Canal 1 bajo, 2 melodía, 3 acordes, 10 batería (GM), reloj de BAJO y **CC 20 en el canal 16** con la sección (0 intro, 20 build, 40 drop, 60 break, 80 drop 2, 100 outro). Ver `tools/serial_midi/` para el puente al computador (115200 baud); compila con `-DMIDI_BAUD=31250` para un sintetizador por el DIN-5.

Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash Mode: DIO. Requiere FastLED. `-DDIAG=1` imprime por Serial0 la carga y el nivel (sin MIDI).

## microSD (guarda toda la escena sola)

Pines: **SCK 14 · MOSI 15 · MISO 16 · CS 17** (los que usaba el programa de reproducción de `cancion.wav` que traía la placa; no están en el esquema del repo, hay que confirmarlos con tu módulo). Tarjeta en FAT32.

- Guarda `/percusynth/productor.txt` (texto legible) 3 s después del último cambio: capa en curso, los 4 valores de cada capa, patrón de bajo, octava, groove, percusión, progresión, modo y ARREGLO AUTO. Escribe un temporal y lo renombra: nunca queda a medias.
- Al encender recupera la escena. Las perillas no saltan: usan el escalado de valor (LED 5 ámbar y una raya en la pantalla hasta que cada una coincide con lo guardado).
- La pantalla muestra **SD** en la cabecera cuando la tarjeta está lista y "ESCENA CARGADA DE SD" al arrancar.
- Sin tarjeta todo funciona igual y se reintenta cada 5 s. Con `-DDIAG=1` imprime por Serial0 el estado de la tarjeta y cada guardado.
