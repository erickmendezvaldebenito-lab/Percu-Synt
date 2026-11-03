# Z — temas desmenuzados desde la microSD + capas sintéticas ancladas

Estado: **funcionando** (versión guardada el 2026-10-11). Diseño completo, mediciones y hoja de ruta en [`PLAN.md`](PLAN.md).

## Qué hay en esta carpeta

| Archivo | Para qué |
|---|---|
| `z.ino` | el firmware: motor a 48 kHz / 24 bit, tres stems en streaming desde la microSD (anillos en PSRAM), 3 capas sintéticas (BAJO · RITMO · SONIDOS) que siguen la posición del tema, 4.ª capa STEMS, menú de temas en pantalla, escena guardada en la tarjeta, MIDI |
| `cargador/cargador.ino` | herramienta temporal: copia archivos del computador a la microSD por USB (con CRC) sin sacar la tarjeta |
| `PLAN.md` | diseño, formato elegido, mediciones de la tarjeta y de la CPU, fases |
| `../../tools/z_prep/` | `preparar_z.py` (deja un tema listo para la tarjeta) y `subir_a_sd.py` (lo manda por USB al cargador) |

## Controles

BTN1 corto = siguiente capa · **BTN1 mantenido 0,7 s = menú de temas** · BTN2 = STOP/PLAY de la capa · POT1 = volumen de la capa · POT2 = tempo (en STEMS: volumen de la batería).
Capa STEMS: BTN2 play/stop del tema · BTN3 salta 8 compases · BTN4 inicio · BTN5 solo (todos → batería → bajo → otros) · POT2/3/4 = batería/bajo/otros.

## Cómo se compila (importante)

- ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · **PSRAM: OPI PSRAM** · **optimización `-O2`**.
  - arduino-cli: `--fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=dio,PSRAM=opi --build-property "compiler.optimization_flags=-O2"`.
  - Arduino IDE: `platform.local.txt` con `compiler.optimization_flags=-O2`.
- Requiere FastLED. Pantalla OLED SSD1306 (SDA 21 / SCL 38) opcional.
- microSD en FAT32 (SPI: SCK 14 · MOSI 15 · MISO 16 · CS 17, a 20 MHz).
- `-DDIAG=1`: diagnóstico por el puerto (CPU, tarjeta, compás, huecos). Con BTN1 deshabilitado y el tema arrancando solo.

## Medido (tema sonando, 3 stems + capas)

CPU 50–63 % medio / 65–82 % máximo, 0 retrasos, 0 huecos de lectura; tarjeta ~1,5 MB/s a 20 MHz sin errores.
