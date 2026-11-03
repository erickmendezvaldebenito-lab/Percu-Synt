# Z — temas desmenuzados desde la microSD, sincronizados con las capas del PercuSynth

> Estado: **`z.ino` funcionando en la placa**: motor a 48 kHz / 24 bit, 4.ª capa STEMS, tres stems en streaming desde la microSD (anillos en PSRAM), reloj de las capas anclado a la posición del tema. Medido: 90 s seguidos, CPU 50-63 % medio / 65-82 % máximo, **0 retrasos, 0 huecos de lectura**, compás 1 → 47 (1,9 s por compás: clavado a 128 BPM).

## Qué es

Un reproductor de temas "desmenuzados" (stems) que lee la microSD y toca **bateria / bajo / otros** a la vez, y sobre ellos las **capas sintetizadas** del PercuSynth (BAJO · RITMO · SONIDOS y las que se sumen) **ancladas a la posición del tema**, de modo que nada se desfase nunca.

Reglas de la familia que se mantienen: BTN1 cambia de capa · POT1 volumen · BTN2 STOP/PLAY de la capa · POT2 tempo (en Z el tempo lo manda el tema; POT2 pasa a ser *velocidad de loop/pitch* según capa — ver "Pendientes").

## El tema que ya está ordenado: "Extarnel" (medido, no supuesto)

| Dato | Valor |
|---|---|
| Tempo / compás | 128 BPM, 4/4 (pulso 468,75 ms, compás 1,875 s) |
| Tonalidad | Re menor, 440 Hz |
| Duración | 352,9 s = **188 compases** (cuenta 189 con la cola) |
| Stems | bateria, bajo, otros (+ mezcla, conteo de 1 compás, metrónomo); originales a 96 kHz / 24 bit |
| Alineación | suma de stems vs mezcla: **correlación 0,993 a desfase 0**; todos empiezan en t = 0 y la rejilla de compases cae en t = 0 (±28 ms de la envolvente) |
| Entradas | bateria desde 1,4 s (tiempo 4 del compás 1) · otros ~1,6 s · bajo ~7,9 s (compás 5) |
| Estructura (por niveles) | 1-4 intro · 5-16 entra el bajo · 17-72 tema · 73-80 baja la bateria · 81-104 tema · **109-112 break sin bateria** · 113-180 tema · 181-189 salida |
| Armonía | la carta (Moises) sólo marca D / Dm: compases 17 D · 51 Dm · 56 D · 64 Dm · 70 D · 116 Dm · 122 D · 123 Dm/D · 124 Dm · 126 D/Dm · 130 D. Es un tema de pedal en Re con color mayor/menor, no una progresión |

Los originales viven en `~/Downloads` (no se copian al repositorio). `tools/z_prep/preparar_z.py` genera la carpeta de la tarjeta.

## Estructura de la microSD (FAT32, raíz): varios temas, menú en pantalla

```
/TEMAS/
  Extarnel/                 una carpeta por tema (el nombre de la carpeta es lo que sale en el menú, o `nombre=` del z.cfg)
    z.cfg                   bpm, tonalidad ("D menor"), nombre …
    armonia.txt             un acorde por compás ("17  D", "51  Dm", …)
    audio48/                MAESTRO: 48 kHz · 24 bit · estéreo
      bateria.wav  bajo.wav  otros.wav          (los que lee Z)       mezcla.wav  conteo.wav  metronomo.wav
    audio44/                PLAN B: 44,1 kHz · 16 bit (las mismas pistas)
  OtroTema/ …
/percusynth/                escena guardada (productor.txt: capa, valores, patrones, **último tema**)
```

- Un tema nuevo = una carpeta más en `/TEMAS` (la crea `tools/z_prep/preparar_z.py`). Aparece solo en el menú.
- Al primer arranque, si existe `/Z` (la carpeta de la versión anterior) y no existe `/TEMAS`, la placa la renombra a `/TEMAS/Extarnel`.
- El tema usa su propio BPM del `z.cfg` (el reloj es `tick = zPos · BPM / 12 000 000`, entero y exacto para BPM con dos decimales) y su tonalidad.

### Menú de temas

| Gesto | Qué hace |
|---|---|
| **BTN1 mantenido 0,7 s** | abre el menú (el resto sigue sonando) |
| BTN3 / BTN4 | sube / baja en la lista (el tema cargado lleva `*`) |
| **BTN5** | carga el tema elegido (vuelve a la capa STEMS y lo deja listo; BTN2 = PLAY) |
| BTN2, o soltar BTN1 | cierra el menú sin cargar |
| BTN1 corto | sigue siendo "siguiente capa" (ahora al soltar) |

Al encender: las capas sintéticas quedan en STOP (el tema es el protagonista), la primera pantalla es STEMS, y se carga el último tema usado.

## Qué se midió en la placa (microSD de 64 GB, módulo de 6 cables, 4 MB escritos y leídos de vuelta a cada velocidad)

| SPI | Lectura | Escritura | Errores | Bloque de 4 KB más lento |
|---|---|---|---|---|
| 4 MHz | 427 KB/s | 432 KB/s | 0 | 15,1 ms |
| 10 MHz | 921 KB/s | 956 KB/s | 0 | 8,0 ms |
| **20 MHz** | **1.496 KB/s** | 1.594 KB/s | 0 | 6,5 ms |
| 26 MHz | 1.497 KB/s | 1.601 KB/s | 0 | 6,2 ms |
| 40 MHz | 1.495 KB/s | 1.601 KB/s | 0 | 6,3 ms |

Conclusión: el techo está en **~1,5 MB/s** (la biblioteca/la tarjeta, no los cables); **20 MHz ya lo alcanza y 26/40 MHz no suben más**, así que se usa 20 MHz (margen eléctrico). El peor bloque tarda 6,5 ms: con colchones de ≥ 80 ms por pista no se nota.

## Decisión de formato: 48 kHz / 24 bit estéreo (maestro)

- Los stems originales son de **96 kHz**: bajar a 48 kHz es una decimación exacta 2:1 (sin remuestreo raro) y la mezcla ya viene a 48 kHz. Nada de 44,1 kHz "a medias".
- El DAC PCM5102 acepta 24/32 bit a 48 kHz: se gana piso de ruido (−144 dB vs −96 dB, y sin dither) y no hay conversión de frecuencia en tiempo real.
- Ancho de banda: **3 stems = 864 KB/s (58 % del techo medido)**; la mezcla sola 288 KB/s; mezcla + 3 stems 1.152 KB/s (77 %, sólo para comparar A/B).
- Costo: **todo el motor pasa a 48 kHz** (`SAMPLE_RATE 48000`: constantes derivadas de SR, reverb algo más corta/brillante por 8 %) y la CPU sube ~9 % (hoy 55-60 % medio, 75-80 % máximo → ~60-65 % y ~83-88 %). **Se verifica en la placa antes de adoptarlo**; si no cabe, se usa `audio44/` sin tocar nada más (ya está convertido).

## Capas (hoy y propuestas)

| Capa | Estado | Qué hace |
|---|---|---|
| BAJO · RITMO · SONIDOS | existen (Productor) | síntesis, ahora ancladas al tick del tema y a la armonía de `armonia.txt` |
| STEMS | nueva | volumen y silencio por stem, y un filtro tipo DJ (pasa-altos ↔ pasa-bajos) por stem |
| REPETIR | nueva | loop de 1/2/4 compases y beat-repeat anclados a la rejilla (nunca sale de fase) |
| FX | nueva | eco/reverb por stem, riser/impacto en las entradas, corte de bajo en los breaks |
| ESCENAS | nueva | guardar/recuperar mezcla + capas por compás (`/Z/escenas/`) |
| GRABAR | nueva | MIDI y/o audio de lo que tocas a la tarjeta, con el compás marcado |

## Fases

0. **Ordenar** — hecho: material medido, convertido (48/24 y 44,1/16) y estructurado; este plan.
1. **SD medido** — hecho (tabla). Falta: reproducir **un** stem sin cortes desde la tarjeta (colchón en RAM, lectura en el núcleo 0 a 20 MHz).
2. **Tres stems** sincronizados con contador de compases en la pantalla; seek por compás. *Hecho y medido.* (Salto de compás hoy = corte de ~0,1 s; el salto sin hueco necesita un segundo juego de anillos.)
3. **Capas sintéticas ancladas** al tick: *hecho en `z.ino`* (con tema suenan desde `zPos`; sin tema, relojes libres como en Productor). Armonía por compás desde `/Z/armonia.txt` (Dm/D).
4. **STEMS, REPETIR, FX**.
5. **ESCENAS y GRABAR**.

## Pendientes de decisión

- ¿POT2 en Z? (el tempo ya no es libre): propuesta = *pitch/velocidad* sólo en modo libre, y en modo tema = **cantidad de filtro DJ** del stem activo.
- ¿Mono/32 kHz si el SD no alcanza para los 3 stems estéreo?
- ¿La mezcla (`mezcla.wav`) se usa o basta con los 3 stems? (la suma ya correlaciona 0,993).
- Derechos: el tema es de un tercero; los archivos son para tu uso personal en tu instrumento, no se distribuyen ni se guardan en el repositorio (viven en `~/Downloads` y en la tarjeta).

## Capa STEMS (4.ª capa de `z.ino`)

| Control | Función |
|---|---|
| BTN1 | siguiente capa (BAJO → RITMO → SONIDOS → STEMS) |
| BTN2 | PLAY / STOP del tema |
| POT1 | volumen general de los stems |
| POT2 / POT3 / POT4 | volumen de bateria / bajo / otros (aquí POT2 deja de ser tempo: el tempo lo manda el tema) |
| BTN3 | salta 8 compases adelante |
| BTN4 | vuelve al inicio |
| BTN5 | solo: todos → bateria → bajo → otros |

Con `-DDIAG=1` el tema arranca solo y se imprime por el puerto: estado de la tarjeta, anillos, compás actual y huecos de lectura.

## Cómo se compila Z (importante para la CPU)

- Placa: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · **PSRAM: OPI PSRAM**.
- **Optimización `-O2`** (el core compila con `-Os` por defecto): con `-Os` el motor a 48 kHz + 3 stems daba 70-78 % medio y hasta 105 % con retrasos; con `-O2` y la copia del bloque de stems a RAM interna: 50-63 % medio, 65-82 % máximo, 0 retrasos.
  - arduino-cli: `--build-property "compiler.optimization_flags=-O2"`.
  - Arduino IDE: crea `platform.local.txt` junto al `platform.txt` del core ESP32 con la línea `compiler.optimization_flags=-O2`.
- Tarjeta: FAT32 con la carpeta `/Z` de `tools/z_prep/` (formato 48 kHz / 24 bit en `/Z/audio48/`). Cableado SPI: SCK 14 · MOSI 15 · MISO 16 · CS 17, a 20 MHz.
