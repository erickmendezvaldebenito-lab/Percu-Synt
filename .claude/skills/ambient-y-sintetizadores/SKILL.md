---
name: ambient-y-sintetizadores
description: Ambient, texturas y síntesis de sonido para el PercuSynth — cómo se hace un pad, un drone, un piano eléctrico, un bajo, un bombo, una voz de formantes o un riser en tiempo real en el ESP32-S3, con los costos medidos. Úsala al crear o cambiar una voz, una textura, un efecto o un ambiente.
---

# Ambient y sintetizadores — recetas y límites medidos

## Ambient: qué lo hace ambient

- **Ritmo armónico muy lento:** un acorde por ciclo de 4 compases (9-38 s). Sin progresiones funcionales ni cadencias V–i: la tónica sostiene y el movimiento es un cambio de modo.
- **Drón continuo** en la tónica (raíz + quinta + octava abajo) que libera al resto de tener bajo propio.
- **Cada nota dura hasta la siguiente**: la separación es la duración. Temas de contorno claro (llamada 1-5-8, lamento 8-7-6-5, suspiro 6-5).
- Ataques de 0,5-4 s, colas de reverb oscuras (~2,7 kHz en el lazo), eco que se oscurece en cada repetición, estéreo ancho sin mover nada solo.
- Modos "de cine": eólico, frigio, dórico (nórdico), mixolidio, lidio, frigio dominante, doble armónica, armónica menor. Los temas deben tocar los grados 2, 4, 6 y 7: la nota que define el modo casi nunca está en el acorde.

## Bloques de síntesis disponibles (todos baratos por muestra)

| Bloque | Cómo |
|---|---|
| Sierra sin alias | 6 tablas limitadas en banda (Lanczos), una por octava; elegir por frecuencia |
| Seno / armónicos | tabla de 1024 con interpolación; sumas de armónicos horneadas (piano eléctrico, cuerdas 1/n) |
| Filtros | SVF TPT (Zavalishin), coeficientes por bloque interpolados por muestra; pasa-bajos 24 dB = dos en serie |
| Saturación | cúbica suave `x − x³/3`; **sólo voces con UNA parcial fuerte** |
| FM | razón del modulador **siempre > 2** |
| Metal | parciales 1 : 2,76 : 5,4, cola corta |
| Voz de formantes | sierra por 3 pasa-banda (F1/F2/F3 de A·E·I·O·U con deslizamiento); "más robótico" = monótona |
| Riser | ruido en pasa-banda con barrido exponencial + seno ascendente; al soltar, impacto |
| Reverb | placa de Dattorro a media frecuencia, normalizada por su realimentación |
| Eco | ping-pong de corchea con dos cabezas y crossfade al cambiar el tiempo (nunca deslizar la cabeza: cambia el tono) |

## Recetas

- **Pad:** 3-4 sierras + SVF con ataque lento + pasa-altos ~140 Hz.
- **Piano eléctrico:** 4 senos con armónicos (3.ª-5.ª-7.ª-9.ª), SVF cuya frecuencia baja con cada golpe; el golpe nuevo entra tras un fundido de 1,5 ms.
- **Bajo:** sub (seno + 2.º armónico) + sierra por SVF 24 dB con envolvente; slide por glissando de fase.
- **Bombo:** barrido de seno 140→base + clic de ruido; la base cae en la raíz del acorde.
- **Caja:** ráfaga de ruido con cola + cuerpo tonal 190 Hz corto. **Rimshot:** ráfaga de 20 ms + tono 330 Hz.

## Reglas de calidad (cada una nació de un defecto medido)

DC blocker a la salida · limitador con ganancia suavizada y el techo de agudos DESPUÉS · nunca liberar una voz cortándola (pasar a tau 1,5 ms y liberar a −82 dB) · coeficientes nunca saltan en el bloque · sin bit-crush ni decimación (alias) · dither TPDF sólo a 16 bit; a 24 bit no hace falta · ningún `expf/powf/tanf/floorf/división` por muestra.

## Costos en el ESP32-S3 a 240 MHz

`sinf` 1,0 µs · `powf` 2,9 · `expf` 1,2 · `tanf` 0,8 · `floorf` 0,4 · división 0,4 · tabla+interp ~0,5. Presupuesto por buffer de 128: **2 902 µs a 44,1 kHz, 2 667 µs a 48 kHz**.
Compilar con **`-O2`** (el core usa `-Os`): fue la mejora más grande medida (de 70-78 % medio con retrasos a 50-63 %). Datos de PSRAM: copiar por bloques a RAM interna antes de decodificar por muestra.
