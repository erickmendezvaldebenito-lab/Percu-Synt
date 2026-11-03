# TouchDesigner — Humo (etapa 2: funciona en TouchDesigner 2025.33230; el aspecto aún NO iguala la referencia)

Port a TouchDesigner del modo **Humo GPU** del navegador (`../index.html`). **No pude ejecutarlo** (TouchDesigner no está instalado en esta máquina),
así que es un andamio: lo corres, copias los `[FALLÓ]` y los errores rojos que salgan, y los corrijo.

## Pasos
1. Instala TouchDesigner (derivative.ca). La licencia gratuita no comercial limita la imagen a 1280×1280; el 4K requiere licencia de pago (verifícalo en su sitio).
2. Abre un proyecto vacío. Alt+T abre el **Textport** y pega una sola línea:

       exec(open('/Users/erick/Desktop/Percu-Synth-main/tools/campos_visual/touchdesigner/build_humo.py', encoding='utf-8').read())

3. Se crea `/project1/humo`. Abre el nodo **salida** para ver el resultado. En `audio_in` elige tu mezcladora (el script busca "DJM").
4. Copia al chat todo lo que salga con `[FALLÓ]` o en rojo.

## Qué contiene
- `sim.glsl` — simulación de 65.536 partículas (RGBA32F) con feedback: venas (campo curl), núcleo (resorte+remolino) y haces (gravedad newtoniana).
- `seed.glsl`, `accum.glsl` — estado inicial y acumulación con persistencia sobre papel blanco.
- `frame_exec.py` — cada cuadro: bandas de audio → golpes con umbral adaptativo → ráfagas, núcleo, viento → uniformes del GLSL TOP.
- `build_humo.py` — crea la red y conecta todo.

## Qué falta (etapas siguientes)
Dos capas con persistencia distinta (venas vs. arcos), más partículas con un MAT GLSL (instancing por TOP→CHOP limita a ~100k), grabación 4K y lanzar con tu audio en vivo.


## Estado (etapa 2)
- **Ejecutado de verdad:** la red `humo` se crea sin errores de parámetros (salvo el audio), corre a 60 fps con 65.536 partículas y tres capas
  (venas / núcleo / haces) dibujadas como **trazos estirados por la velocidad**, con persistencia propia, multiplicadas sobre blanco.
- **Cómo se depura sin ver el Textport:** `lanzador.py` escribe `inicio.txt`/`error_build.txt`/`log_build.txt`; `frame_exec.py` escribe `estado.txt`
  (errores y avisos por operador) y `salida.png` cada ~3 s. Quien depura lee esos archivos.
- **Límite de licencia gratuita:** 1280×1280 (por eso 720×1280). El 4K requiere licencia de pago.
- **Audio:** TouchDesigner no lista la DJM-750MK2 (solo micrófonos). `audio_a_td.py` (con `../grabar_mixer`) manda graves/medios/agudos por UDP al puerto 7001 (`udp_in`).
  Sin audio entra DEMO.
- **Pendiente:** núcleo más compacto y negro (hoy la nube de núcleo se reparte demasiado), arcos visibles (haces con velocidad suficiente), más partículas
  (instancing por TOP→CHOP limita ~65k–250k; para millones usar un MAT GLSL), grabar a 720×1280 o 4K.

## Vista en vivo sin buscar nodos (ver.html)
`frame_exec.py` guarda `salida_vivo.png` ~10 veces por segundo y `ver.html` lo muestra en el navegador (se refresca solo, sin parpadeo).
Ábrelo con: `open -a "Google Chrome" ver.html`. Primero hay que ejecutar `lanzador.py` en el Textport de TouchDesigner.
