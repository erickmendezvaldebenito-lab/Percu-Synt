#!/usr/bin/env python3
"""Prepara un tema "desmenuzado" (stems + mezcla + conteo + metrónomo + carta de acordes) para el PercuSynth (proyecto Z).

Deja una carpeta lista para copiar a la RAÍZ de la microSD (FAT32). Cada tema vive en SU carpeta dentro de /TEMAS y el menú del
PercuSynth (BTN1 mantenido) los lista todos:

  TEMAS/<carpeta>/
    z.cfg            metadatos del tema (BPM, compases, tonalidad, rutas)
    armonia.txt      un acorde por compás (sale de la carta MusicXML)
    secciones.txt    nivel de cada stem cada 4 compases (para ver dónde entra y sale cada cosa)
    audio48/         bateria.wav  bajo.wav  otros.wav  mezcla.wav  conteo.wav  metronomo.wav   (48 kHz / 24 bit / estéreo)  <- MAESTRO
    audio44/         las mismas seis pistas a 44.1 kHz / 16 bit / estéreo                                       <- plan B

Sólo macOS (usa afconvert, que ya viene en el sistema). Los archivos originales NO se tocan ni se copian al repositorio.

Uso:  python3 preparar_z.py --origen ~/Downloads --salida ~/Desktop/Z_SD [--carpeta Extarnel --nombre "Extarnel"]
"""
import argparse, array, math, os, re, subprocess, sys, tempfile, wave
import xml.etree.ElementTree as ET

def afconvert(src, dst, hz=44100, canales=2, bits=16):
    cmd = ["afconvert", "-f", "WAVE", "-d", f"LEI{bits}@{hz}", "-c", str(canales), "--src-quality", "127", src, dst]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"afconvert falló con {src}: {r.stderr.strip()}")

def cargar_mono(path):
    w = wave.open(path, "rb"); a = array.array("h"); a.frombytes(w.readframes(w.getnframes())); return a

def acordes_de_musicxml(path):
    """Devuelve {compás: [(parte_del_compás 0..1, 'D', 'major'|'minor')]} y el número total de compases."""
    raiz = ET.parse(path).getroot()
    res = {}; total = 0
    for m in raiz.iter("measure"):
        n = int(m.get("number")); total = max(total, n)
        notas = [e for e in m if e.tag in ("note", "harmony")]
        pos = 0; durs = [int(e.findtext("duration") or 0) for e in notas if e.tag == "note"]
        compas_dur = sum(durs) or 1
        for e in notas:
            if e.tag == "note":
                pos += int(e.findtext("duration") or 0)
            else:
                paso = e.findtext("root/root-step") or "?"
                alt = int(e.findtext("root/root-alter") or 0)
                nombre = paso + ("#" if alt > 0 else "b" if alt < 0 else "")
                res.setdefault(n, []).append((pos / compas_dur, nombre, e.findtext("kind") or "major"))
    return res, total

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--origen", default=os.path.expanduser("~/Downloads"))
    ap.add_argument("--salida", default=os.path.expanduser("~/Desktop/Z_SD"))
    ap.add_argument("--bpm", type=float, default=128.0)
    ap.add_argument("--tonalidad", default="D menor")
    ap.add_argument("--nombre", default="Extarnel")
    ap.add_argument("--carpeta", default=None, help="nombre de la carpeta en /TEMAS (por defecto, el nombre del tema sin espacios raros)")
    a = ap.parse_args()
    o = a.origen
    stems96 = os.path.join(o, "02-stems")
    fuentes = {
        "bateria.wav":   os.path.join(stems96, "Drums.wav"),
        "bajo.wav":      os.path.join(stems96, "Bass.wav"),
        "otros.wav":     os.path.join(stems96, "Other.wav"),
        "mezcla.wav":    os.path.join(o, "02.wav"),
        "conteo.wav":    [os.path.join(o, d, f) for d in os.listdir(o) if d.startswith("02. Extarnel-D minor") and os.path.isdir(os.path.join(o, d))
                          for f in os.listdir(os.path.join(o, d)) if "Count-in" in f][0],
        "metronomo.wav": [os.path.join(o, d, f) for d in os.listdir(o) if d.startswith("02. Extarnel-D minor") and os.path.isdir(os.path.join(o, d))
                          for f in os.listdir(os.path.join(o, d)) if "Metronome" in f][0],
    }
    xml = [os.path.join(o, f) for f in os.listdir(o) if f.endswith("Chord Chart.musicxml")][0]
    carpeta = a.carpeta or re.sub(r"[^A-Za-z0-9_-]+", "_", a.nombre).strip("_")[:22] or "Tema"
    z = os.path.join(a.salida, "TEMAS", carpeta)
    # Formato MAESTRO 48 kHz / 24 bit: los stems originales son de 96 kHz (decimación exacta 2:1, sin remuestreo raro) y la mezcla ya viene a 48 kHz.
    # Plan B 44,1 kHz / 16 bit por si el motor no aguanta 48 kHz. Medido en la placa: la tarjeta sostiene ~1,5 MB/s (3 stems a 48/24 = 864 KB/s).
    for carpeta, hz, bits in (("audio48", 48000, 24), ("audio44", 44100, 16)):
        os.makedirs(os.path.join(z, carpeta), exist_ok=True)
        for dst, src in fuentes.items():
            if not os.path.exists(src): raise SystemExit("falta " + src)
            print(f"convirtiendo {os.path.basename(src)} -> {carpeta}/{dst}", flush=True)
            afconvert(src, os.path.join(z, carpeta, dst), hz=hz, bits=bits)
    audio = os.path.join(z, "audio44")
    # métricas del audio ya convertido
    w = wave.open(os.path.join(audio, "mezcla.wav")); dur = w.getnframes() / w.getframerate(); w.close()
    pulso = 60.0 / a.bpm; compas = 4 * pulso
    # niveles por 4 compases (de versiones a 4 kHz mono)
    niveles = {}
    with tempfile.TemporaryDirectory() as td:
        for k in ("bateria", "bajo", "otros"):
            t = os.path.join(td, k + ".wav"); afconvert(os.path.join(audio, k + ".wav"), t, hz=4000, canales=1)
            niveles[k] = cargar_mono(t)
    sr = 4000; paso = int(4 * compas * sr)
    lineas = ["# compases   bateria_dB  bajo_dB  otros_dB   (cada 4 compases; -99 = silencio)"]
    nsec = int(dur / (4 * compas)) + 1
    for s in range(nsec):
        fila = []
        for k in ("bateria", "bajo", "otros"):
            seg = niveles[k][s * paso:(s + 1) * paso]
            if len(seg) == 0: fila.append(-99.0); continue
            rms = math.sqrt(sum(x * x for x in seg) / len(seg)) / 32768.0
            fila.append(20 * math.log10(rms) if rms > 1e-5 else -99.0)
        lineas.append(f"{s * 4 + 1:4d}-{s * 4 + 4:<4d}  " + "  ".join(f"{x:8.1f}" for x in fila))
    open(os.path.join(z, "secciones.txt"), "w").write("\n".join(lineas) + "\n")
    # armonía por compás (el acorde se mantiene hasta el siguiente cambio)
    ch, total_xml = acordes_de_musicxml(xml)
    n_comp = int(dur / compas) + 1
    sal = ["# compás  acorde   (D = mayor, Dm = menor; se mantiene hasta el siguiente cambio; carta: Moises AI Studio, muy escueta)"]
    actual = "-"
    for c in range(1, n_comp + 1):
        if c in ch:
            tx = []
            for parte, nom, kind in ch[c]:
                tx.append(nom + ("m" if "minor" in kind else ""))
            actual = "/".join(tx)
        sal.append(f"{c:4d}  {actual}")
    open(os.path.join(z, "armonia.txt"), "w").write("\n".join(sal) + "\n")
    cfg = f"""# Proyecto Z — tema: {a.nombre}
nombre={a.nombre}
bpm={a.bpm:g}
compas=4/4
tonalidad={a.tonalidad}
afinacion_hz=440
formato_maestro=48000 Hz / 24 bit / estereo
formato_plan_b=44100 Hz / 16 bit / estereo
canales=2
duracion_s={dur:.3f}
compases_total={n_comp}
pulso_ms={pulso * 1000:.3f}
compas_ms={compas * 1000:.3f}
inicio_rejilla_ms=0
carpeta_maestro=audio48
carpeta_plan_b=audio44
pistas=bateria.wav,bajo.wav,otros.wav,mezcla.wav,conteo.wav,metronomo.wav
armonia=armonia.txt
secciones=secciones.txt
"""
    open(os.path.join(z, "z.cfg"), "w").write(cfg)
    print("\nlisto:", z)
    for r, _, fs in os.walk(z):
        for f in sorted(fs):
            p = os.path.join(r, f); print(f"  {os.path.relpath(p, a.salida):28s} {os.path.getsize(p) / 1e6:8.1f} MB")

if __name__ == "__main__":
    main()
