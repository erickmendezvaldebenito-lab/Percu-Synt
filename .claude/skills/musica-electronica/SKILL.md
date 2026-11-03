---
name: musica-electronica
description: Conocimiento de producción de música electrónica aplicado al PercuSynth — tempos y sensación por género, forma de canción y transiciones, programación de batería y bajo, armonía, carriles de mezcla, sincronía y MIDI. Úsala al componer un patrón, un tema o un estilo nuevo, o al decidir cómo debe sonar o estructurarse algo.
---

# Música electrónica — lo que sabe este proyecto

Sólo la **estética** de los géneros y artistas: nunca se transcriben melodías ni letras de terceros (el firmware trae una tabla `TUYO_N` para que el usuario escriba la suya).

## Tempos y sensación

| Género | BPM | Rasgos que ya están construidos |
|---|---|---|
| Techno ácido / oscuro | 124-134 | bombo seco y saturado, bajo ácido que respira, hats a contratiempo, pad con cola (`techno_capas`) |
| Techno melódico | 118-128 | arpegio sobre pad de novena, riser + drop (`productor_capas`) |
| Electro de máquina | 120 (100-140) | bajo Minimoog rígido en 16avos, caja/palmada electrónica, voz de formantes monótona, sin swing (`kraftwerk_capas`) |
| Dub / roots | 60-100 (72) | one drop (el 1 vacío, golpe en el 3), skank en el contratiempo, sub largo, eco de corchea que se oscurece (`dub_capas`) |
| Downtempo / trip-hop | 70-110 (84) | swing de 1 tick, sub con notas largas y slides, piano con novenas, caja con cuerpo (`bruma_capas`) |

## Forma (la diferencia entre un loop y un tema)

Frases de 8 y 16 compases. Forma probada: INTRO 8 · BUILD 8 · DROP 16 · BREAK 8 · DROP 2 16 · OUTRO 8 = 64 compases (~2 min a 124 BPM).
Transiciones: riser los últimos 2 compases, **silencio de bombo y bajo en el último tiempo**, redoble de toms, impacto al entrar la sección, el primer compás de cada frase de 4 sale limpio y la variación crece hasta el 4.º.
La variación vive en máscaras por compás (quitar/octavar notas, añadir hats), no en azar por golpe.

## Batería

- Bombo cae en cada tiempo (steppers/techno) o en 1 y 3 (rockers); caja/palmada en 2 y 4, o sólo en el 3 (one drop).
- Hats: contratiempo abierto, 16avos con acentos, o 8avos; la perilla de hats mueve caída y brillo.
- **Una cabeza por instrumento:** bombo, caja, toms, hat y sub son monofónicos con fast-kill de 3 ms.
- Rellenos sólo al final de la frase y *reemplazan* el patrón, no se apilan.
- Dos envolventes por golpe (tono + ruido); un metal se hace con parciales inarmónicos (1 : 2,76 : 5,4), nunca con barridos rápidos (suenan a pájaro).

## Bajo

Sub de seno + capa de sierra filtrada opcional (el "cuerpo"). Notas largas hasta la siguiente en dub/downtempo; staccato de máquina (≤ 2 semicorcheas) en electro. Intervalos: raíz, 2.ª menor, 3.ª, 4.ª, 5.ª, 7.ª, octava. **El bombo afina a la raíz del acorde** (plegada a 41-82 Hz) para que no peleen.

## Armonía

Modos menores y pedales; progresiones cortas (i, i–VI, i–iv–i–v, i–VI–III–VII); acordes con novena en downtempo (3.ª-5.ª-7.ª-9.ª sin fundamental: la toca el sub). En escalas exóticas sólo tríadas consonantes. La armonía la lleva una sola capa y manda al bajo, al piano, al arpegio y al bombo.

## Mezcla (carriles)

Sub 40-60 Hz sólo bombo y bajo · acordes con pasa-altos ~140 Hz · arpegio ~210 Hz · hats en banda 7-10 kHz (nunca pasa-altos) · techo de agudos ~10-13 kHz **después** del limitador · pumping del bombo sobre bajo, acordes y cola de reverb · compresor de bus que no actúa y limitador que casi nunca (picos 0,6-0,7). Un exceso de nivel suena a "saturado": primero baja el nivel, luego mira el compresor.

## Sincronía y MIDI

24 PPQN. En Z el reloj es la posición de muestras del tema (cero deriva). MIDI: canal 1 bajo, 2 melodía, 3 acordes, 10 batería GM, reloj del bajo, CC 20 en el canal 16 = sección. Un compás a 128 BPM / 48 kHz = 90 000 muestras exactas.
