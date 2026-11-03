---
name: percusynth-ux
description: Usabilidad (UX/UI) del PercuSynth — cómo debe sentirse, mostrarse y comunicarse un instrumento de 5 botones, 4 perillas, 6 LED y una pantalla OLED 128×64. Úsala al diseñar o cambiar controles, pantalla, LEDs, menús, mensajes, guardado o cualquier cosa que el usuario toque o lea en la placa.
---

# PercuSynth — UX / UI de un instrumento de 5 botones y 4 perillas

Reglas sacadas de iteraciones reales con el usuario (Techno en Capas → Productor → Z). Cada una tiene un porqué medido o una queja concreta detrás.

## Gestos fijos (no se reasignan)

| Control | Función |
|---|---|
| BTN1 corto | capa siguiente (al soltar) |
| BTN1 mantenido 0,7 s | menú en pantalla (en Z: temas) |
| POT1 | volumen de la capa |
| BTN2 | STOP / PLAY de la capa |
| POT2 | tempo de la capa (si un tema manda el tempo, POT2 pasa a otra cosa y la pantalla lo dice) |
| BTN3-5, POT3-4 | lo propio de cada capa (nunca más de 3 botones y 2 perillas por capa) |

El usuario lo pidió "siempre igual": la memoria muscular vale más que cualquier mejora local.

## Principios

1. **Un botón, una función.** Sin combos ocultos ni páginas. Si algo necesita un modo, se llama en pantalla.
2. **Descubrible:** lo que hace cada botón y perilla está *en la pantalla*, no en el manual. Mapa de capa = nombre y valor de cada perilla + función de cada botón.
3. **Todo gesto contesta:** un aviso de 1,5 s en la última línea ("GROOVE ROTO", "SALTO AL COMPAS 17") y un destello en los LED. Un botón que no pasa nada es un error de diseño: si no hay efecto, dilo ("SIN TEMA", "SIN TARJETA").
4. **La perilla no salta.** Al volver a una capa se usa escalado de valor (la perilla recorre su camino y el valor guardado se estira para encontrarla); el LED 5 ámbar y una raya en la pantalla muestran que aún no coincide. Nunca "recoger" a ciegas.
5. **Nada se mueve solo** sobre algo que el usuario cree estar sosteniendo. La variación automática se declara (perilla de evolución, modo AUTO).
6. **Los cambios de patrón entran en la rejilla** (siguiente tiempo; la armonía en el siguiente compás), no al instante: suena a música, no a error. Excepción: lo que se toca (golpes, FX) suena en el flanco de presión.
7. **Guarda solo.** La escena se escribe 3 s después del último cambio y se recupera al encender; nunca pidas "guardar". La tarjeta ausente no debe romper nada.
8. **Un fallo no es silencio:** nombra la causa en la pantalla en una línea y deja el resto funcionando.

## Pantalla 128×64 (fuente 5×7, 21 caracteres por línea, filas de 9 px)

- Fila 0 = barra invertida: `n/N CAPA` a la izquierda, estado o posición (`C047/185`, `DROP 05/16`) a la derecha.
- Filas 1-4 = una perilla cada una: `Pn NOMBRE`, barra de 44 px, valor. Marca vertical = dónde está la perilla mientras no coincide.
- Filas 5-6 = botones en dos columnas (`B2 STOP  B3 PATRON`); la última fila es el aviso del último gesto.
- Anuncio de capa: 1,1 s a pantalla completa (nombre en tamaño 3) al cambiar de capa o arrancar.
- Menú: lista de ≤ 5 filas, la elegida invertida, pie con las teclas (`B3 SUBE B4 BAJA B5 CARGA`).
- **Texto en MAYÚSCULAS y sin tildes** (la fuente no las tiene). Etiquetas ≤ 7-9 caracteres.
- Actualiza sólo las páginas que cambiaron y desde una tarea de baja prioridad: un refresco completo bloqueaba ~35 ms.

## LED (6 SMD)

LED 0 = pulso del tiempo (color = capa; rojo tenue = en STOP; parpadeo ámbar = esperando el compás); LED 1-4 = lo propio de la capa; **LED 5 = estado del instrumento** (ámbar = alguna perilla sin coincidir, rojo = fallo de CPU, blanco tenue = normal). Al mover una perilla los LED 1-5 hacen de medidor 0,9 s.

## Tiempos

Botón → sonido < 10 ms (medido: dentro del primer buffer de 2,9 ms), antirrebote 40 ms, mantener = 700 ms, aviso 1,5 s, anuncio de capa 1,1 s, refresco de pantalla ≤ 100 ms. Ningún dibujo, escritura de SD ni `Serial` puede retrasar el audio ni la lectura de los controles (tareas separadas por núcleo).

## Lista de verificación antes de entregar un cambio

- ¿Cada control nuevo aparece en la pantalla con nombre y valor? ¿Contesta con aviso?
- ¿Respeta los gestos fijos? ¿Hay algún combo o modo oculto?
- ¿Qué ve el usuario si falta la tarjeta, el tema, la PSRAM o el archivo?
- ¿Se guarda y se recupera? ¿Las perillas no saltan al recuperar?
- ¿Se midió que el bucle de control no se bloquea (mayor tiempo entre vueltas) y que la CPU de audio no sube? (`-DDIAG=1`)

## Antipatrones ya vistos

Perillas "pegadas" (escalado sin indicador), modos escondidos tras SHIFT, tempo por capa sin decir cuál manda, demasiados botones por capa, textos con tildes, avisos que tapan el mapa, refrescos de pantalla que congelan los controles.
