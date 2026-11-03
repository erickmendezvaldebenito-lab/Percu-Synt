#!/usr/bin/env python3
"""Genera un samples.h PROVISORIO para matriz_oscura.ino (sin las voces reales del noticiero).

- Bancos TITULARES / PRESENTADOR / SENAL: 3 samples cortos sintéticos cada uno (un "pip" de noticiero:
  tonos + ruido, μ-law 44.1 kHz). NO son voces: sólo permiten probar el panel Samples.
- REC_*: los 12 recortes de la percusión extra (3 pistas × 4): ráfagas de ruido/metal con caída a cero.
Los reales los escribe preparar_samples.py desde banco/ (ElevenLabs); con ese samples.h reemplaza éste.
Uso:  python3 generar_samples_provisorio.py   (escribe samples.h al lado)
"""
import math, random, os
SR = 44100
random.seed(7)

def mulaw(x):                      # x en [-1,1] -> byte G.711 (inverso del decodificador del sketch)
    sg = 0x00 if x >= 0 else 0x80
    v = min(int(abs(x) * 32767), 32635) + 132
    e = max(0, v.bit_length() - 8)
    m = (v >> (e + 3)) & 0x0F
    return (~(sg | (e << 4) | m)) & 0xFF

def clip(ms, f0, f1, ruido, metal, caida):
    n = int(SR * ms / 1000); out = []; ph = 0.0; lp = 0.0
    for i in range(n):
        t = i / n; f = f0 + (f1 - f0) * t; ph += 2 * math.pi * f / SR
        tono = math.sin(ph) + (0.6 * math.sin(ph * 2.41) + 0.4 * math.sin(ph * 3.73) if metal else 0)
        lp += 0.35 * ((random.random() * 2 - 1) - lp)
        env = math.exp(-i / (SR * caida)) * min(1.0, i / (SR * 0.002)) * (1 - t) ** 0.5
        out.append(mulaw(max(-1, min(1, (tono * (1 - ruido) + lp * 2.5 * ruido) * env * 0.8))))
    out[-1] = mulaw(0.0)
    return out

bancos = [
    [clip(420, 880, 660, 0.15, False, 0.14), clip(380, 1240, 1240, 0.10, False, 0.12), clip(520, 520, 780, 0.2, True, 0.18)],
    [clip(300, 220, 180, 0.35, False, 0.09), clip(360, 260, 200, 0.30, False, 0.10), clip(280, 310, 240, 0.4, False, 0.08)],
    [clip(500, 150, 90, 0.8, True, 0.15), clip(450, 700, 120, 0.9, True, 0.12), clip(600, 90, 60, 0.7, True, 0.20)],
]
largos_ms = [(40, 60), (80, 120), (160, 260)]
rec = []
for e in range(3):
    for k in range(4):
        ms = random.randint(*largos_ms[e])
        rec.append(clip(ms, random.choice([180, 420, 900, 2100]), random.choice([120, 600, 1500]),
                        random.uniform(0.3, 0.9), k % 2 == 1, ms / 1000 / 3.0))

def arr(nombre, datos):
    return "static const uint8_t %s[] PROGMEM = {\n%s\n};\n" % (nombre, ",".join(str(b) for b in datos))

L = ["// samples.h PROVISORIO — generado por generar_samples_provisorio.py. NO son las voces del noticiero:",
     "// son sonidos sintéticos para que matriz_oscura compile y se pueda probar. Reemplázalo por el real.", "#pragma once", "#include <stdint.h>", ""]
flat = [s for b in bancos for s in b]
for i, d in enumerate(flat): L.append(arr("SMP_%d" % i, d))
for i, d in enumerate(rec):  L.append(arr("REC_%d" % i, d))
L.append("const uint8_t *const SMP_DATOS[%d] = { %s };" % (len(flat), ", ".join("SMP_%d" % i for i in range(len(flat)))))
L.append("const uint32_t SMP_LARGO[%d] = { %s };" % (len(flat), ", ".join(str(len(d)) for d in flat)))
L.append("const uint8_t SMP_FORMATO[%d] = { %s };   // 1 = μ-law" % (len(flat), ", ".join("1" for _ in flat)))
L.append("const int SMP_INICIO[4] = { 0, 3, 6, 9 };")
L.append("const uint8_t *const REC_DATOS[12] = { %s };" % ", ".join("REC_%d" % i for i in range(12)))
L.append("const int32_t REC_LARGO[12] = { %s };" % ", ".join(str(len(d)) for d in rec))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "samples.h"), "w").write("\n".join(L) + "\n")
print("samples.h:", sum(len(d) for d in flat) + sum(len(d) for d in rec), "bytes de audio")
