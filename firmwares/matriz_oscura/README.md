# matriz_oscura

Firmware de 8 estilos oscuros/agresivos con un panel por instrumento, 6 paneles y matriz WS2812 de 32×8.
Toda la descripción, los controles y los ajustes del IDE están en el encabezado de `matriz_oscura.ino`.

## Estado de `samples.h`

El sketch hace `#include "samples.h"` (39 samples del "noticiero del futuro", ~2,2 MB, voces de ElevenLabs).
**Ese archivo real no está en el repo.** Esta carpeta trae uno **PROVISORIO**, generado con
`generar_samples_provisorio.py`: 9 sonidos sintéticos (3 por banco) + 12 recortes de percusión extra.
No son voces, sólo sirven para que el sketch compile y se pruebe todo lo demás.

Para usar los reales: copia tu `samples.h` encima de éste (mismo nombre, misma carpeta).
La interfaz que el sketch espera: `SMP_INICIO[4]`, `SMP_DATOS[]`, `SMP_LARGO[]`, `SMP_FORMATO[]` (1 = μ-law),
`REC_DATOS[12]`, `REC_LARGO[12]` (recortes μ-law a 44,1 kHz).

## Arduino IDE
ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · Partition Scheme: Huge APP · FastLED instalada.
