#!/usr/bin/env python3
"""Copia la carpeta Z (y lo que quieras) a la microSD del PercuSynth por USB, sin sacar la tarjeta del módulo.

Necesita la placa con firmwares/z/cargador/cargador.ino. Verifica cada archivo con CRC32 (lo que la placa escribió y lo que lee de vuelta de la tarjeta).

Uso:  python3 subir_a_sd.py ~/Desktop/Z_SD/Z --destino /Z [--solo audio48] [--puerto /dev/cu.usbmodemXXXX]
"""
import argparse, glob, os, sys, time, zlib
import serial

BLOQUE = 16384

def abrir(puerto):
    p = puerto or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
    if not p: sys.exit("no veo la placa (/dev/cu.usbmodem*)")
    s = serial.Serial(); s.port = p; s.baudrate = 115200; s.dtr = False; s.rts = False; s.timeout = 2; s.write_timeout = 20
    s.open()
    t0 = time.time()
    while time.time() - t0 < 12:                       # al abrir el puerto la placa puede reiniciarse: se espera el "OK cargador"
        s.reset_input_buffer(); s.write(b"HOLA\n")
        l = s.readline().decode(errors="replace").strip()
        if l.startswith("OK cargador"): print(p, "->", l); return s
        time.sleep(0.5)
    sys.exit("la placa no contesta (¿tiene el cargador cargado?)")

def orden(s, txt, esperar=("OK", "FIN", "ERR", "CRC", "LISTO")):
    s.write((txt + "\n").encode())
    while True:
        l = s.readline().decode(errors="replace").strip()
        if not l: sys.exit(f"sin respuesta a: {txt}")
        if l.startswith(esperar): return l

def crc_local(path):
    c = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b: break
            c = zlib.crc32(b, c)
    return c & 0xFFFFFFFF

def subir(s, local, remoto):
    n = os.path.getsize(local)
    r = orden(s, f"PUT {remoto} {n}")
    if r != "LISTO": sys.exit(f"{remoto}: {r}")
    c = 0; enviados = 0; t0 = time.time(); ult = 0
    with open(local, "rb") as f:
        while enviados < n:
            b = f.read(BLOQUE)
            s.write(b); c = zlib.crc32(b, c); enviados += len(b)
            ack = s.read(1)
            if ack != b"A": sys.exit(f"\n{remoto}: sin confirmación en {enviados} bytes (recibí {ack!r})")
            if time.time() - ult > 1.0:
                ult = time.time(); v = enviados / (time.time() - t0) / 1024
                print(f"\r  {os.path.basename(remoto):16s} {enviados / n * 100:5.1f} %  {v:6.0f} KB/s", end="", flush=True)
    fin = s.readline().decode().strip()
    c &= 0xFFFFFFFF
    esperado = f"FIN {c:08x} {n}"
    if fin != esperado: sys.exit(f"\n{remoto}: la placa dice '{fin}', esperaba '{esperado}'")
    r = orden(s, f"CRC {remoto}")                       # relee de la tarjeta
    if r != f"CRC {c:08x} {n}": sys.exit(f"\n{remoto}: al releer de la tarjeta '{r}', esperaba 'CRC {c:08x} {n}'")
    dt = time.time() - t0
    print(f"\r  {os.path.basename(remoto):16s} {n / 1e6:8.1f} MB  ok  CRC {c:08x}  {n / dt / 1024:6.0f} KB/s  releido y verificado")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("origen"); ap.add_argument("--destino", default="/Z")
    ap.add_argument("--solo", default=None, help="sólo esta subcarpeta (p. ej. audio48) y los .cfg/.txt")
    ap.add_argument("--puerto", default=None)
    a = ap.parse_args()
    s = abrir(a.puerto)
    archivos = []
    for raiz, _, fs in os.walk(a.origen):
        for f in sorted(fs):
            p = os.path.join(raiz, f); rel = os.path.relpath(p, a.origen)
            if a.solo and "/" in rel and not rel.startswith(a.solo + "/"): continue
            archivos.append((p, a.destino + "/" + rel))
    archivos.sort(key=lambda x: os.path.getsize(x[0]))   # primero lo pequeño
    total = sum(os.path.getsize(p) for p, _ in archivos)
    print(f"{len(archivos)} archivos, {total / 1e6:.0f} MB")
    t0 = time.time()
    for p, r in archivos: subir(s, p, r)
    print(f"\nlisto en {(time.time() - t0) / 60:.1f} min. Contenido de {a.destino}:")
    print(orden(s, "DF"))

if __name__ == "__main__":
    main()
