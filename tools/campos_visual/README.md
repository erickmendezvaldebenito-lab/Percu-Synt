# campos_visual

Visualizador en el estilo de **"Campos conectados"**: puntos rojos que son cargas
(+ fuentes, − sumideros) y líneas de campo de colores que nacen en las fuentes, terminan
en los sumideros o se escapan del cuadro. **Cada golpe de la percusión hace nacer un punto**;
todo lo demás se mueve despacio y con suavidad.

Un solo HTML (sin build). Abre `index.html` en Chrome o Edge.

## Entradas (combinables)

| Entrada | Cómo |
|---|---|
| **Audio** | Salida de la PercuSynth a la entrada de línea del computador (o el micrófono frente al parlante). Detecta golpes por flujo espectral en 3 bandas con umbral adaptativo. No reproduce el audio, así que no hay realimentación. |
| **MIDI** | `MIDI_Drum`, `drum_midi_leds`, `trance_midi_leds`… (Web MIDI). Notas GM de bombo→grave, caja/toms→medio, hats/platos→agudo; la nota fija la posición horizontal. |
| **Serial** | Firmware de control de `video_synth` (115200 baud): BTN1-2 grave, BTN3 medio, BTN4-5 agudo, piezo = golpe medio. |
| **Sin placa** | Teclado `Z X C`, `Espacio`, clic en pantalla, o **Demo** (arranca solo). |

## Mezcladora USB multicanal (Pioneer DJM-750MK2): `grabar_mixer` + `puente_mixer.js`

Medido con la DJM-750MK2 (12 canales a 48 kHz): la mezcla principal llega por los canales **9-10** (medido con CoreAudio: pico −23 dB; los demás en silencio). ffmpeg la replicaba en los canales 3-12 y los **1-2 van en silencio**.
Por eso Chrome (solo ve 1-2) y QuickTime (idem) graban silencio, y **ffmpeg/avfoundation pierde ~8 % de las muestras**
(`-t 8` entrega 7,3 s de audio), que suena como "chicharra" y cortes. `grabar_mixer.swift` captura con CoreAudio
(AVAudioEngine) y entrega exactamente lo pedido: 8,00 s en 8 s, 20,00 s en 20 s.

    cd tools/campos_visual
    swiftc -O grabar_mixer.swift -o grabar_mixer      # una sola vez
    ./grabar_mixer mezcla.wav 60                       # WAV limpio de 60 s (canales 9,10); sin segundos = hasta Ctrl+C
    node puente_mixer.js                               # puente en vivo para la página (usa grabar_mixer; si no existe, ffmpeg)

Con el puente corriendo, **🎙 Escuchar** toma ese audio (línea Entrada "✔ Mixer (puente…)"). El audio entra a un AudioWorklet con
cola circular (reproduce continuo, ajusta el ritmo ±1 % para no acumular retraso; medido: 0 vaciados en 20 s).
Para el video final lo más limpio es `./grabar_mixer mezcla.wav` y luego **📁 Cargar audio** + **🎬 Renderizar MP4 fluido**.
La Terminal necesita permiso de micrófono (macOS lo pide al primer uso). Variables: `DISPOSITIVO`, `PAR=9,10`, `PORT`.

## Audio del computador (música del Mac)
Botón **🖥 Audio del computador**:
1. **Sin cuadro de Chrome (recomendado):** `audio_sistema.swift` (ScreenCaptureKit, macOS 13+) captura TODO lo que suena en el Mac (Spotify, Apple Music, YouTube…) y el puente
   lo sirve en `/sistema`. Pasos: `swiftc -O audio_sistema.swift -o audio_sistema` (una vez) · en **Ajustes del Sistema → Privacidad y seguridad → Grabación de pantalla y audio del sistema**
   activa la **Terminal** · `node puente_mixer.js` en esa Terminal · pulsa el botón. macOS muestra su propio indicador de grabación, no el banner de Chrome.
2. **Sin puente no se abre ningún cuadro:** el botón avisa. (Mayús+clic usa el respaldo de compartir pestaña de Chrome, que sí muestra su banner.) Para iniciar el puente basta **doble clic en `iniciar_puente.command`**.
   El permiso de macOS se pide **una sola vez** y es inevitable (ninguna app puede grabar el audio del Mac sin él).

## Sincronía con la música
- El audio que llega por el puente (mixer o sonido del Mac) se **analiza con ~80 ms de retraso** (antes 400 ms): ese audio no suena en la página, solo se mide.
- **Detección rítmica por saltos de nivel**: espectro en dB (FFT 2048, sin suavizado) en 3 bandas — graves 35–130 Hz, medios 200–2500 Hz, agudos 5–14 kHz. Un golpe es un salto de varios dB sobre el promedio de los ~200 ms anteriores (tiempo de espera 110/80/55 ms). El umbral sigue al slider **Sensibilidad**. Medido con música real: 18 golpes de graves, 40 de medios y 21 de agudos en ~10 s, con intervalos medianos de 150–200 ms.
- Cada golpe empuja los puntos con un resorte amortiguado (graves: empuje grande hacia fuera; medios: medio; agudos: temblor corto) y las líneas se deforman con ellos, sin cambiar colores (slider **Vibración con el ritmo**, 0 lo apaga) y las barras del panel saltan con cada golpe.
- **Sincronía: retraso de la imagen** (0–400 ms): si ves la imagen *adelantada* al sonido (parlantes o audífonos Bluetooth tienen 100–250 ms de retraso de salida), súbelo hasta que caigan juntos.

## Cómo se ve lo que suena

- **Grave** (bombo): punto grande y fuerte, abajo, con muchas líneas.
- **Medio** (caja/toms): punto mediano, al centro.
- **Agudo** (hats/metales): punto fino y de pocas líneas, arriba.
- Un golpe de una banda hace **brillar y girar un poco** las líneas de los puntos de esa banda.
- Cada punto aparece con fade-in, sus líneas **crecen** en ~3 s, deriva a muy baja velocidad y
  se desvanece al envejecer (18–32 s). Hay un enfriamiento por banda (0,45 s / 0,45 s / 1,1 s):
  una ráfaga de hats hace brillar, no llena la pantalla.

## Ocho modos (tecla `V`) y dos efectos (Tinta se eliminó)

- **Campo:** líneas de corriente que nacen de los puntos (el diseño original).
- **Malla:** rejilla de líneas que se deforma: los puntos atraen/repelen y cada golpe lanza una onda (tope suave `tanh`).
- **Constelación:** ~70 estrellas a la deriva más los puntos de cada golpe; se unen por cercanía y el golpe enciende y engrosa las líneas de su banda.
- **Ondas:** anillos que nacen de cada punto y se expanden lento; el golpe los ondula y lanza un anillo grande.
- **Flujo:** 1.400 partículas que siguen el campo de los puntos (con giro) y dejan estela larga; el golpe las acelera.
- **Humo GPU (WebGL2):** calibrado contra tu grabación de referencia. 409.600 partículas en la GPU, tres clases: **venas** (~88 %, siguen un campo de ruido sin divergencia y dibujan la red tenue que cubre el cuadro), **núcleo** (~12 %, resorte corto + remolino: el punto casi negro con rayitas radiales) y **haces** (~0,1 %, gravedad newtoniana alrededor del núcleo: cada golpe de bajo/medio lanza un grupo de ~8 con casi la misma velocidad y dibujan arcos elípticos paralelos, como en la referencia). Dos capas con persistencia distinta (venas+núcleo ≈ 0,9 s, haces ≈ 2,4 s) que se multiplican sobre blanco. El slider **Caos** sube las octavas altas del ruido. **Forma (revisada con cuadros a tamaño completo):** no explota hacia afuera; es una "molécula" compacta: núcleo casi negro con fibras, **órbitas elípticas cerradas de 3-8 curvas paralelas** (gravedad, apoapsis ≈2-3 radios del núcleo, lanzadas con cada golpe) y humo fibroso que se abre en abanico con un viento lento que gira. **Calibración medida** (9 cuadros de tu grabación vs. 3 cuadros míos, mismo encuadre vertical): brillo medio 0,944 vs 0,90–0,91; píxeles más oscuros que 0,25 (el núcleo) 1,3 % vs 1,1–1,6 %; más oscuros que 0,5: 2,5 % vs ~5 %; velo tenue (0,9–0,985): 38 % vs 46–61 %. Las ganancias están en `GLT` (`av` venas, `ac` núcleo, `al` haces). Probado solo en un Chrome de prueba con GPU por software (SwiftShader); si tu navegador no tiene WebGL2 con texturas float, avisa y vuelve a Campo.
- **Abanico curvo / Abanico curvo claro (teclas 9 y menú):** el mismo abanico 3D pero cada rayo es un arco que se abre hacia su lado (forma de lira), respira y se abre de golpe con los graves.
- **Abanico / Abanico claro (3D):** líneas finísimas de bloques de puntos (rejilla en dos paredes que se curvan hacia la cámara y ondulan) a UN punto de convergencia que también se mueve en 3D; la cámara gira despacio con perspectiva. Cada golpe enciende un bloque: graves abajo, medios al centro, agudos arriba. *Abanico* = blanco sobre negro; *Abanico claro* = negro sobre blanco.
- **Espejo** (`K`): off → 2 lados → 4 lados (simetría, copia el cuadro ya dibujado: casi gratis).
- **Ciclo de color** (`R`): la paleta rota de tono despacio.

Costo medido por cuadro (≈400×720, Chrome sin GPU): Ondas 2,2 ms · Campo 3,3 · Malla 20,6 · Flujo 1,5 · Abanico 1,1. Antes (1280×900, Chrome sin GPU): Campo 3,8 ms · Malla 12 ms · Constelación 20 ms · Ondas 2,5 ms · Flujo 1 ms.

## Video para Reels (con el audio sincronizado)

1. **📱 Formato Reel** muestra la vista vertical 9:16 a **1080×1920** (liviana: la vista NO va a 4K). El tamaño del **video final** se elige en el menú
   "Video final: 4K / 1080p" junto a *Renderizar*: durante la exportación el lienzo pasa a ese tamaño y al terminar la vista vuelve a 1080×1920.
   Si el equipo no alcanza, la vista baja sola su resolución (medidor del panel); al cambiar de modo vuelve al 100 %.
2. **📁 Cargar audio** y elige tu grabación de la PercuSynth (wav/mp3). Es lo mejor: los golpes se detectan directo del archivo, sin micrófono ni retraso.
3. **⏺ Grabar video**: el audio suena, el visualizador reacciona y se graba imagen + audio hasta el final del archivo; al terminar se descarga un `.mp4` (o `.webm` si el navegador no soporta mp4).
   Sin archivo, graba en vivo lo que escucha el micrófono (🎙 Audio).

**Para que salga fluido, usa 🎬 Renderizar MP4 fluido** (en vez de ⏺ Grabar en vivo): no graba en tiempo real, calcula
CADA cuadro a 60 fps con paso fijo (1/60 s), analiza el audio del archivo con una FFT propia y codifica H.264 + AAC con
WebCodecs (`mp4-muxer.js`, incluido en la carpeta, sin internet). Siempre sale sin cuadros perdidos, aunque tarde más que
la canción: la barra inferior muestra el avance y el tiempo restante. Probado en Chrome sin GPU: 1080×1920 a 60 fps
rindió ~3 cuadros/s; con la GPU del Mac será bastante más rápido, pero un tema largo en 4K puede tardar varios minutos.

**Medido (Chrome sin GPU, modo Abanico, 4K 2160×3840 a 60 fps):** 1,5 s de audio → 92 cuadros en 13 s (~0,14 s por cuadro, MP4 de 12,6 MB).
Estimado para 60 s: ~8 min. **Grabar en vivo no sirve a 4K** (pierde cuadros): al pulsarlo en 4K la página baja sola a 1080p.
Un medidor en el panel muestra los cuadros por segundo y la **calidad** (resolución adaptativa: si el equipo no alcanza, baja la resolución sola, 50–100 %).

Mantén la ventana de Chrome al frente mientras graba (en segundo plano el navegador frena la animación).
Instagram acepta mp4; si salió webm: `ffmpeg -i entrada.webm -c:v libx264 -c:a aac salida.mp4`.

## Controles

`1`–`8` elegir modo (o el menú del panel) · `Z/X/C` golpe grave/medio/agudo · `Espacio` al azar · clic = golpe ahí · `A` audio · `M` MIDI ·
`S` serial · `D` demo · `V` modo · `K` espejo · `R` ciclo de color · `P` paleta (arcoíris / por punto) · `H` panel · `F` pantalla completa.
El panel y el cursor se esconden solos a los 5 s sin mover el ratón (para proyectar).

Sliders: **Sensibilidad** (umbral de detección de audio), **Lentitud del movimiento**
(deriva y giro, curva cuadrática: abajo casi quieto) y **Densidad de líneas**.

## Notas técnicas

- Campo 2D de cargas puntuales `E = Σ qᵢ·d/(|d|²+ε)`; las líneas se integran con punto medio
  (paso 6,5 u) normalizando el campo, desde `N` ángulos equiespaciados alrededor de cada fuente.
- Los buffers de cargas tienen 64 entradas (vivas + las que se desvanecen); si se agrandan los
  límites de `MAXC` hay que mantener `CAP` por encima.
- Costo medido: ~3,4 ms por cuadro con ~13 fuentes a 1536×1152.

## Grabar sonido en vivo → video 4K (recomendado para calidad)
**🎬 Grabar y recrear en 4K** (opcional, lento; no repite exactamente lo que viste): con el audio conectado (mixer, sonido del Mac o micrófono) graba solo el sonido mientras ves la imagen en vivo; al detener, renderiza el video fuera de tiempo real (4K o 1080p según el menú, 60 fps, sin cuadros perdidos). **⏺ Grabar en vivo** guarda lo que muestra la pantalla (1080×1920 en Reel, ~17 Mbps, ~25 cuadros/s medidos en un Mac real), por eso se ve menos nítido.
