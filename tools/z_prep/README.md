# z_prep

Prepara un tema desmenuzado para el proyecto **Z** (`firmwares/z/PLAN.md`).

```bash
python3 preparar_z.py --origen ~/Downloads --salida ~/Desktop/Z_SD
```

Entra: `02-stems/{Bass,Drums,Other}.wav` (96 kHz / 24 bit), `02.wav` (mezcla), la carpeta del tema con `Count-in` y `Metronome` (mp3) y la carta `…Chord Chart.musicxml`.
Sale: `~/Desktop/Z_SD/TEMAS/<carpeta>/` con `audio48/` (48 kHz / 24 bit / estéreo, maestro) y `audio44/` (44,1 kHz / 16 bit, plan B), `z.cfg`, `armonia.txt` y `secciones.txt`. Se copia la carpeta `TEMAS` a la raíz de la microSD (FAT32); cada tema nuevo es una carpeta más y aparece solo en el menú del PercuSynth.

Sólo macOS (usa `afconvert`). No toca los originales ni copia audio al repositorio.
