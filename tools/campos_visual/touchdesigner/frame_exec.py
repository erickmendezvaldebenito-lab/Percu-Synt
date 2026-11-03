# frame_exec.py — Execute DAT (Frame Start). Mismas reglas que drawSmokeGL() del navegador:
# análisis de audio por banda con umbral adaptativo -> golpes -> ráfagas; núcleo que deriva; viento que gira; uniformes del GLSL TOP.
import math, random

CARPETA = '/Users/erick/Desktop/Percu-Synth-main/tools/campos_visual/touchdesigner'

def onFrameStart(frame):
    try:
        _frame(frame)
    except Exception:
        import traceback
        try: open(CARPETA + '/error.txt', 'w', encoding='utf-8').write(traceback.format_exc())
        except Exception: pass

def _frame(frame):
    b = parent()
    st = b.fetch('st', None)
    if st is None:
        st = dict(t=absTime.seconds, T=0.0, pe=[0, 0, 0], fm=[0, 0, 0], fv=[1, 1, 1], last=[-9, -9, -9], pk=[1e-6] * 3, cont=[0, 0, 0],
                  cx=0.0, cy=0.0, vx=0.0, vy=0.0, exp=0.0, swl=0.0, frame=0, bursts=[], imp=[0, 0, 0])
        b.store('st', st)
    now = absTime.seconds
    dt = max(1e-3, min(0.05, now - st['t'])); st['t'] = now; st['T'] += dt; T = st['T']
    U = (2.0 * 9 / 16) / 800.0            # unidades de mundo por "unidad de 800 px" (el mundo mide 1.125 x 2)
    R0 = 0.05 * (2.0 * 9 / 16)

    # --- audio: prioridad 1) UDP desde audio_a_td.py (DJM)  2) CHOP "spec"  3) DEMO ---
    niveles = None; fuente = 'demo'
    u = op('udp_in')
    if u is not None and u.numRows > 0:
        try:
            msg = u[u.numRows - 1, 0].val.strip()
            if msg != st.get('udp_msg'): st['udp_msg'] = msg; st['udp_t'] = now
            if now - st.get('udp_t', -9) < 0.5:
                v = [float(x) for x in msg.split()[:3]]
                if len(v) == 3: niveles = v; fuente = 'udp'
        except Exception: pass
    if niveles is None and op('spec').numChans > 0:
        sp = op('spec')[0].vals
        n = len(sp); hz = (op('audio_in').par.rate.eval() / 2.0) / max(1, n)
        def banda(f0, f1):
            i0, i1 = max(1, int(f0 / hz)), min(n - 1, int(f1 / hz) + 1)
            return sum(sp[i0:i1 + 1]) / max(1, (i1 - i0 + 1))
        niveles = [banda(30, 160), banda(160, 1800), banda(1800, 9000)]; fuente = 'spec'
    if niveles is None:
        if not st.get('avisado'):
            print('[AVISO] sin audio (ni UDP ni CHOP spec): uso DEMO. Para la DJM: python3 audio_a_td.py'); st['avisado'] = True
        dm = st.setdefault('demo', [0.0, 0.0, 0.0]); paso = int(T / (60.0 / 92 / 4))
        if paso != st.get('paso'):
            st['paso'] = paso; s16 = paso % 16
            if s16 in (0, 6, 10): dm[0] = 1.0
            if s16 in (4, 12): dm[1] = 1.0
            if s16 % 2 == 0 and random.random() < 0.55: dm[2] = 0.7
        for i in range(3): dm[i] *= math.exp(-dt * 8.0)
        niveles = [x * 0.02 for x in dm]
    st['fuente'] = fuente
    sens = 0.55
    k = 3.2 - 2.9 * sens
    for i, e in enumerate(niveles):
        e = math.log1p(400.0 * e)
        flux = max(0.0, e - st['pe'][i]); st['pe'][i] = e
        st['fm'][i] = st['fm'][i] * 0.985 + flux * 0.015
        st['fv'][i] = st['fv'][i] * 0.985 + (flux - st['fm'][i]) ** 2 * 0.015
        thr = st['fm'][i] + k * math.sqrt(st['fv'][i]) + 0.02
        st['pk'][i] = max(e, st['pk'][i] * 0.997, 0.2)
        st['cont'][i] = (min(1.0, e / st['pk'][i])) ** 2.2
        if flux > thr and e > 0.05 and now - st['last'][i] > 0.08:
            st['last'][i] = now; fuerza = max(0.35, min(1.0, 0.35 + (flux - thr) * 2.0))
            if i == 0:
                st['exp'] = min(2.2, st['exp'] + 1.2 * fuerza); a = random.uniform(0, 6.2832)
                st['vx'] += math.cos(a) * fuerza * 380 * U; st['vy'] += math.sin(a) * fuerza * 380 * U
                st['nb'] = st.get('nb', 0) + 1; st['imp'][0] += fuerza; st['bursts'].append((random.uniform(0, 6.2832), 0.86 + 0.12 * fuerza))
            elif i == 1:
                st['swl'] = min(2.2, st['swl'] + 1.1 * fuerza); st['imp'][1] += fuerza; st['bursts'].append((random.uniform(0, 6.2832), 0.84 + 0.12 * fuerza))
            else:
                st['imp'][2] += fuerza
    st['bursts'] = st['bursts'][-6:]
    st['exp'] *= math.exp(-dt * 1.1); st['swl'] *= math.exp(-dt * 0.8)

    # --- núcleo: deriva por Lissajous y rebota con los golpes (coordenadas de mundo: x en [-0.56,0.56], y en [-1,1]) ---
    tt = T * 0.12 * 0.95
    tx = 0.4 * math.sin(tt * 1.3) * 0.56 + 0.15 * math.sin(tt * 3.1) * 0.56
    ty = 0.32 * math.cos(tt * 1.1) + 0.12 * math.sin(tt * 2.7)
    st['vx'] += (tx - st['cx']) * dt * 3.2; st['vy'] += (ty - st['cy']) * dt * 3.2
    d = math.exp(-dt * 2.4); st['vx'] *= d; st['vy'] *= d
    st['cx'] = max(-0.2, min(0.2, st['cx'] + st['vx'] * dt)); st['cy'] = max(-0.4, min(0.4, st['cy'] + st['vy'] * dt))

    # --- viento que gira ---
    wa = 0.9 * math.sin(T * 0.13) + 0.6 * math.sin(T * 0.31 + 1)
    wm = (60 + 120 * st['exp']) * U

    caos = 1.0
    rafaga = st['bursts'].pop(0) if st['bursts'] else None
    sim = op('sim'); c = st['cont']
    def vec(i, x, y, z, w):
        for suf, v in zip('xyzw', (x, y, z, w)): getattr(sim.par, 'vec%dvalue%s' % (i, suf)).val = v
    vec(0, dt, T, caos, U)
    vec(1, c[0], c[1], c[2], R0)
    vec(2, 1.0 if rafaga else 0.0, float(random.randrange(32)), rafaga[0] if rafaga else 0.0, 430.0 * U * (rafaga[1] if rafaga else 0.0))
    vec(3, st['imp'][0], st['imp'][1], st['imp'][2], float(st['frame']))
    vec(4, st['cx'], st['cy'], st['vx'], st['vy'])
    vec(5, math.cos(wa) * wm, math.sin(wa) * wm, st['exp'], st['swl'])
    st['imp'] = [0, 0, 0]; st['frame'] += 1
    # persistencia y oscuridad de cada capa de acumulación: (tau en s, oscuridad por partícula)
    for nom, (tau, osc) in {'v': (0.9, 0.06), 'n': (0.5, 0.30), 'a': (1.6, 0.8)}.items():
        a_ = op('acc_' + nom); a_.par.vec0valuex.val = 1.0 - math.exp(-dt / tau); a_.par.vec0valuey.val = osc
    # --- vista en vivo para el navegador: PNG ~6 veces por segundo (ver.html lo refresca). Nunca en el cuadro 0 ni .jpg (colgó TouchDesigner) ---
    if st['frame'] > 10 and st['frame'] % 10 == 0:
        try: op('salida').save(CARPETA + '/salida_vivo.png', asynchronous=True)
        except Exception as e: st['err_vivo'] = str(e)
    # --- diagnóstico para quien depura: estado y captura cada ~3 s ---
    if st['frame'] % 180 == 1:
        try:
            a = op('audio_in')
            info = 'rafagas=%d fuente=%s frame=%d fps=%s device=%s audio_in.numChans=%d spec.numChans=%d canales=%d\n' % (st.get('nb', 0), st['fuente'], st['frame'], project.cookRate, a.par.device.eval(), a.numChans, op('spec').numChans, op('canales').numChans)
            for nombre in ('seed', 'fb', 'sim', 'inst_v', 'pos_v', 'geo_v', 'cam', 'render_v', 'acc_v', 'inst_a', 'pos_a', 'render_a', 'acc_a', 'acc_n', 'final', 'salida', 'udp_in', 'audio_in'):
                o = op(nombre)
                if o is None: info += '%s: NO EXISTE\n' % nombre; continue
                e, w = o.errors(), o.warnings()
                if e or w: info += '--- %s ---\nERRORES: %s\nAVISOS: %s\n' % (nombre, e, w)
                if 'compile' in (e + w).lower():
                    idat = op('info_' + nombre) or parent().create(infoDAT, 'info_' + nombre)
                    idat.par.op = nombre
                    info += 'LOG DE COMPILACIÓN:\n%s\n' % idat.text
            sm = op('pos_a'); info += 'pos_a: numChans=%d numSamples=%d\n' % (sm.numChans, sm.numSamples)
            try:
                info += 'pos_a: singleset=%s crop=%s opciones_crop=%s sim=%dx%d render=%dx%d\n' % (sm.par.singleset.eval(), sm.par.crop.eval(), list(sm.par.crop.menuNames), op('sim').width, op('sim').height, op('render_a').width, op('render_a').height)
            except Exception as e: info += 'pos diag fallo: %s\n' % e
            open(CARPETA + '/estado.txt', 'w', encoding='utf-8').write(info)
            op('salida').save(CARPETA + '/salida.png')
        except Exception as e:
            open(CARPETA + '/estado.txt', 'w', encoding='utf-8').write('diag fallo: %s' % e)
    return
