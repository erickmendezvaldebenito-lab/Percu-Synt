# serial_midi

Puente **serie → MIDI** para usar el PercuSynth como controlador en el computador (Ableton Live, Surge XT, Vital…).

La placa saca MIDI por el pin 43; el puente USB (CH343) lo lleva al computador como puerto serie, y `serial_midi.py` lo convierte en un puerto MIDI virtual llamado **PercuSynth** (macOS/CoreMIDI).

```bash
pip3 install --user pyserial mido python-rtmidi
python3 serial_midi.py            # deja la ventana abierta mientras tocas
```

- Cierra el Monitor Serie del Arduino antes: el puerto no puede estar ocupado.
- Firmware con salida MIDI: `firmwares/kraftwerk_capas` (bajo = canal 1, melodía/tema = canal 2, acordes = canal 3, batería = canal 10 GM, reloj MIDI del tempo de BAJO).
- El firmware manda a **115200 baud** porque el puente USB de la placa no entrega 31250 baud en macOS. Para un sintetizador por el DIN-5 compila con `-DMIDI_BAUD=31250` (y entonces el computador ya no lo lee).
- En Ableton: Preferencias → Link, Tempo y MIDI → entrada **PercuSynth** → *Track* = On (*Sync* = On si quieres que Live siga su tempo). En cada pista MIDI, "MIDI From" = PercuSynth y elige el canal (Ch. 1 bajo, Ch. 2 melodía, Ch. 3 acordes, Ch. 10 batería).
