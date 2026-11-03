#!/usr/bin/env python3
"""audio_a_td.py — manda a TouchDesigner (UDP 127.0.0.1:7001) el nivel de graves / medios / agudos de la mezcladora.
TouchDesigner (driver por defecto) solo lista micrófonos y NO muestra la DJM-750MK2, y Chrome/ffmpeg ven los canales equivocados.
Este puente usa ../grabar_mixer (CoreAudio, canales 9-10 de la DJM, sin pérdidas) y envía ~60 mensajes por segundo: "graves medios agudos".
USO:  python3 audio_a_td.py          (dejar abierto)      Variables: PUERTO=7001
"""
import os, socket, subprocess, sys, time
import numpy as np

AQUI = os.path.dirname(os.path.abspath(__file__))
CAPTURA = os.path.join(AQUI, '..', 'grabar_mixer')
PUERTO = int(os.environ.get('PUERTO', '7001'))
RATE = 48000            # grabar_mixer entrega la tasa nativa de la DJM (48 kHz)
N = 2048
BANDAS = [(30, 160), (160, 1800), (1800, 9000)]

p = subprocess.Popen([CAPTURA, '--stdout'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ventana = np.hanning(N); frec = np.fft.rfftfreq(N, 1 / RATE)
masc = [(frec >= a) & (frec < b) for a, b in BANDAS]
buf = np.zeros(N, dtype=np.float32); t_envio = 0.0; contador = 0
print('capturando de la DJM… (Ctrl+C para salir)', file=sys.stderr)
try:
    while True:
        datos = p.stdout.read(4 * 800)                      # 800 cuadros estéreo s16 = 16,7 ms
        if not datos or len(datos) < 4: break
        x = np.frombuffer(datos[:len(datos) // 4 * 4], dtype='<i2').astype(np.float32).reshape(-1, 2).mean(axis=1) / 32768.0
        buf = np.concatenate([buf[len(x):], x])[-N:]
        sp = np.abs(np.fft.rfft(buf * ventana)) / N
        niv = [float(sp[m].mean()) * 20.0 for m in masc]
        sock.sendto(('%.6f %.6f %.6f\n' % tuple(niv)).encode(), ('127.0.0.1', PUERTO))
        contador += 1
        if contador % 120 == 0: print('niveles', ['%.4f' % v for v in niv], file=sys.stderr)
except KeyboardInterrupt:
    pass
finally:
    p.kill()
