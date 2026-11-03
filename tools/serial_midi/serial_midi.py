#!/usr/bin/env python3
"""Puente serie -> MIDI para el PercuSynth.

La placa saca MIDI por el pin 43 a 115200 baud; el puente USB (CH343) lo deja pasar al computador.
Este script lee ese puerto serie y lo expone como un puerto MIDI virtual llamado "PercuSynth"
que Ableton Live, Surge XT, Vital, etc. ven como cualquier otro teclado MIDI (macOS: CoreMIDI).

Uso:   python3 serial_midi.py            (busca solo el puerto /dev/cu.usbmodem*)
       python3 serial_midi.py /dev/cu.usbmodemXXXX
Cierra antes el Monitor Serie del Arduino: el puerto no puede estar ocupado.
Requiere:  pip3 install pyserial mido python-rtmidi
"""
import glob, sys, time
import serial, mido

NOMBRE = "PercuSynth"
BAUD = 115200                # el firmware lo manda a 115200 (el puente USB no entrega 31250 en macOS); ver MIDI_BAUD en el .ino


def buscar_puerto():
    while True:
        p = sorted(glob.glob("/dev/cu.usbmodem*"))
        if p:
            return p[0]
        print("Esperando la placa (no veo /dev/cu.usbmodem*)...")
        time.sleep(2)


def main():
    puerto = sys.argv[1] if len(sys.argv) > 1 else buscar_puerto()
    salida = mido.open_output(NOMBRE, virtual=True)
    print(f"Puerto MIDI virtual '{NOMBRE}' creado.  Serie: {puerto} @ {BAUD}")
    print("En Ableton: Preferencias > Link, Tempo y MIDI > entrada 'PercuSynth' > Track = On (y Sync = On para seguir su tempo).")
    parser = mido.Parser()
    cuenta = {}
    ultimo = time.time()
    while True:
        try:
            ser = serial.Serial()
            ser.port = puerto
            ser.baudrate = BAUD
            ser.dtr = False          # no reiniciar la placa al abrir el puerto
            ser.rts = False
            ser.timeout = 0.05
            ser.open()
            while True:
                datos = ser.read(256)
                if datos:
                    parser.feed(datos)
                    for m in parser:
                        salida.send(m)
                        cuenta[m.type] = cuenta.get(m.type, 0) + 1
                if time.time() - ultimo > 5:
                    ultimo = time.time()
                    if cuenta:
                        print("  últimos 5 s:", ", ".join(f"{k} {v}" for k, v in sorted(cuenta.items())))
                        cuenta = {}
        except (serial.SerialException, OSError) as e:
            print("Puerto serie perdido:", e, "- reintento en 2 s")
            time.sleep(2)
            if len(sys.argv) <= 1:
                puerto = buscar_puerto()
        except KeyboardInterrupt:
            print("Adiós.")
            return


if __name__ == "__main__":
    main()
