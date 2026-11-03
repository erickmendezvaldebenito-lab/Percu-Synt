# build_humo.py — construye en TouchDesigner la red "Humo" (etapa 1, ~65.000 partículas).
# USO:  abre TouchDesigner, crea/abre un proyecto vacío, pulsa Alt+T (Textport) y pega UNA línea:
#         exec(open('/Users/erick/Desktop/Percu-Synth-main/tools/campos_visual/touchdesigner/build_humo.py', encoding='utf-8').read())
# Todo lo que no funcione se imprime como  [FALLÓ] ...  — cópiame esas líneas y lo corrijo (no he podido probarlo en TouchDesigner).
import os, math

CARPETA   = '/Users/erick/Desktop/Percu-Synth-main/tools/campos_visual/touchdesigner'
TEX       = 256            # 256x256 = 65.536 partículas (prueba 384 o 512 si tu GPU lo aguanta)
ANCHO, ALTO = 720, 1280    # formato vertical 9:16 dentro del límite de la licencia no comercial (1280x1280); 4K requiere licencia de pago
DISPOSITIVO = 'DJM'        # parte del nombre de tu entrada de audio
CANALES   = 'chan9 chan10' # la DJM-750MK2 manda la mezcla principal por los canales 9-10

fallos = []
lineas = []
def say(*a):
    t = ' '.join(str(x) for x in a); lineas.append(t); print(t)
def P(o, nombres, valor, expr=False):
    """Pone un parámetro probando varios nombres posibles; anota lo que falle."""
    if isinstance(nombres, str): nombres = [nombres]
    for n in nombres:
        try:
            par = getattr(o.par, n)
            if expr: par.expr = valor
            else: par.val = valor
            return True
        except Exception as e:
            last = e
    fallos.append('%s.par.%s = %r  (%s)' % (o.path, '/'.join(nombres), valor, last))
    return False

def crea(padre, tipo, nombre):
    o = padre.create(tipo, nombre); return o

raiz = op('/project1')
if raiz.op('humo'): raiz.op('humo').destroy()
b = crea(raiz, baseCOMP, 'humo')

def texto(nombre, archivo):
    d = crea(b, textDAT, nombre); d.text = open(os.path.join(CARPETA, archivo), encoding='utf-8').read(); return d
d_sim, d_seed, d_acc = texto('sim_glsl', 'sim.glsl'), texto('seed_glsl', 'seed.glsl'), texto('accum_glsl', 'accum.glsl')

def glsl(nombre, dat, w, h, fmt, vecs=()):
    t = crea(b, glslTOP, nombre)
    P(t, 'pixeldat', dat.name)
    P(t, 'outputresolution', 'custom'); P(t, 'resolutionw', w); P(t, 'resolutionh', h)
    P(t, 'format', fmt)
    if vecs:
        try: t.seq.vec.numBlocks = len(vecs)      # la secuencia de uniformes empieza con 1 bloque: hay que crear el resto
        except Exception as e: fallos.append('%s.seq.vec.numBlocks: %s' % (t.path, e))
    for i, n in enumerate(vecs): P(t, 'vec%dname' % i, n)
    return t

# --- simulación en la GPU: semilla -> feedback -> sim (que vuelve al feedback) ---
seed = glsl('seed', d_seed, TEX, TEX, 'rgba32float')
fb   = crea(b, feedbackTOP, 'fb')
sim  = glsl('sim', d_sim, TEX, TEX, 'rgba32float', ['uA', 'uB', 'uC', 'uD', 'uK0', 'uW'])
fb.inputConnectors[0].connect(seed)
sim.inputConnectors[0].connect(fb)
P(fb, 'top', 'sim')

# --- capas de dibujo: cada una = instancias (TOP->CHOP) + geometría + render + acumulación con su propia persistencia ---
d_inst, d_final = texto('inst_glsl', 'inst.glsl'), texto('final_glsl', 'final.glsl')
mat = crea(b, constantMAT, 'negro'); P(mat, ['colorr'], 0.0); P(mat, ['colorg'], 0.0); P(mat, ['colorb'], 0.0)
cam = crea(b, cameraCOMP, 'cam')
if not P(cam, 'projection', 'ortho'): P(cam, 'projection', 'orthographic')
P(cam, 'orthowidth', ANCHO / ALTO * 2.0)       # el mundo mide 2 de alto
P(cam, 'tz', 5)
blanco = crea(b, constantTOP, 'blanco')
for c in ('colorr', 'colorg', 'colorb'): P(blanco, c, 1.0)
P(blanco, 'outputresolution', 'custom'); P(blanco, 'resolutionw', ANCHO); P(blanco, 'resolutionh', ALTO)

def capa(n, clase, grosor, k_estela, lmin, lmax):
    inst = glsl('inst_' + n, d_inst, TEX, TEX, 'rgba32float', ['uCls'])
    inst.inputConnectors[0].connect(sim)
    for suf, v in zip('xyzw', (clase, k_estela, lmin, lmax)): P(inst, 'vec0value' + suf, v)
    pos = crea(b, toptoCHOP, 'pos_' + n); P(pos, 'top', 'inst_' + n); P(pos, 'crop', 'full'); P(pos, 'singleset', True)
    geo = crea(b, geometryCOMP, 'geo_' + n)
    for h in list(geo.children): h.destroy()
    r = crea(geo, rectangleSOP, 'trazo'); P(r, 'sizex', 1.0); P(r, 'sizey', grosor)
    try: r.render = True; r.display = True
    except Exception as e: fallos.append('trazo render/display: %s' % e)
    P(geo, 'material', 'negro')
    P(geo, 'instancing', True); P(geo, 'instanceop', 'pos_' + n)
    P(geo, 'instancetx', 'r'); P(geo, 'instancety', 'g'); P(geo, 'instancerz', 'b'); P(geo, 'instancesx', 'a')
    ren = crea(b, renderTOP, 'render_' + n)
    P(ren, 'geometry', 'geo_' + n); P(ren, 'camera', 'cam'); P(ren, 'lights', '')
    P(ren, 'outputresolution', 'custom'); P(ren, 'resolutionw', ANCHO); P(ren, 'resolutionh', ALTO)
    for c in ('bgcolorr', 'bgcolorg', 'bgcolorb', 'bgcolora'): P(ren, c, 1.0)
    fba = crea(b, feedbackTOP, 'fb_' + n)
    acc = glsl('acc_' + n, d_acc, ANCHO, ALTO, 'rgba8fixed', ['uF'])
    fba.inputConnectors[0].connect(blanco)
    acc.inputConnectors[0].connect(ren); acc.inputConnectors[1].connect(fba)
    P(fba, 'top', 'acc_' + n)
    return acc

# (clase, grosor del trazo, factor de estela, largo mínimo, largo máximo)
cap_v = capa('v', 0, 0.0007, 0.10, 0.0025, 0.030)   # venas: finas y tenues
cap_n = capa('n', 1, 0.0012, 0.04, 0.0015, 0.007)   # núcleo: gruesas y oscuras
cap_a = capa('a', 2, 0.0009, 0.05, 0.0100, 0.060)   # haces: arcos largos y nítidos
fin = glsl('final', d_final, ANCHO, ALTO, 'rgba8fixed')
fin.inputConnectors[0].connect(cap_v); fin.inputConnectors[1].connect(cap_n); fin.inputConnectors[2].connect(cap_a)
salida = crea(b, nullTOP, 'salida'); salida.inputConnectors[0].connect(fin)

# --- audio: dispositivo -> canales de la mezcla -> espectro ---
ain = crea(b, audiodeviceinCHOP, 'audio_in')
def opciones(p):
    try: return list(p.menuNames), list(p.menuLabels)
    except Exception: return [], []
encontrado = False
# la lista por defecto solo trae micrófonos de 2 canales; con el driver nativo (CoreAudio) aparece la mezcladora multicanal
for drv in ('coreaudio', 'native', 'CoreAudio', 'default'):
    try: ain.par.driver = drv
    except Exception: continue
    nombres, labels = opciones(ain.par.device)
    say('[audio] driver=%s  dispositivos: %s' % (drv, labels))
    for n, l in zip(nombres, labels):
        if DISPOSITIVO.lower() in (n + ' ' + l).lower():
            ain.par.device = n; encontrado = True; break
    if encontrado: break
if not encontrado:
    fallos.append('audio_in: no veo un dispositivo con "%s" (ver lista [audio] arriba); driver menú: %s' % (DISPOSITIVO, opciones(ain.par.driver)))
else:
    n_in, l_in = opciones(ain.par.inputs)
    say('[audio] canales de entrada disponibles: %s' % l_in)
    # elige solo los canales 9 y 10 de la DJM (los únicos con la mezcla)
    try: ain.par.inputs = ' '.join(n_in[8:10]) if len(n_in) >= 10 else ' '.join(n_in)
    except Exception as e: fallos.append('audio_in.inputs: %s' % e)
P(ain, 'rate', 48000)
sel = crea(b, selectCHOP, 'canales'); P(sel, 'chops', 'audio_in'); P(sel, 'channames', '*')
mono = crea(b, mathCHOP, 'mono'); mono.inputConnectors[0].connect(sel); P(mono, 'chanop', 'add')  # suma los canales en uno
spec = crea(b, audiospectrumCHOP, 'spec'); spec.inputConnectors[0].connect(mono)

# --- niveles de audio por UDP (audio_a_td.py): la DJM no aparece en la lista de TouchDesigner ---
udp = crea(b, udpinDAT, 'udp_in'); P(udp, 'port', 7001); P(udp, 'clamp', True); P(udp, 'maxlines', 5)

# --- script que corre cada cuadro ---
ex = crea(b, executeDAT, 'frame_exec')
ex.text = open(os.path.join(CARPETA, 'frame_exec.py'), encoding='utf-8').read()
P(ex, 'framestart', True); P(ex, 'active', True)
b.store('TEX', TEX)

# --- vista ---
try: salida.viewer = True
except Exception: pass
pass  # (ventana flotante desactivada: la vista en vivo es ver.html)
say('=' * 60)
say('Red creada en /project1/humo — mira el nodo "salida".')
if fallos:
    say('Hay %d parámetros que no pude poner (probablemente nombres distintos en tu versión). Cópiame esto:' % len(fallos))
    for f in fallos: say('[FALLÓ]', f)
else:
    say('Sin fallos de parámetros.')
say('Si algo se ve vacío: abre el Textport y copia cualquier error rojo.')
try:
    open(os.path.join(CARPETA, 'log_build.txt'), 'w', encoding='utf-8').write('\n'.join(lineas))
except Exception as e: print('no pude escribir el log', e)
