# lanzador.py — ejecuta build_humo.py y, si falla, deja el error completo en error_build.txt (para poder depurar sin ver el Textport).
import traceback, os
_D = '/Users/erick/Desktop/Percu-Synth-main/tools/campos_visual/touchdesigner'
try:
    open(_D + '/inicio.txt', 'w', encoding='utf-8').write('lanzado')
    exec(open(_D + '/build_humo.py', encoding='utf-8').read())
except Exception:
    open(_D + '/error_build.txt', 'w', encoding='utf-8').write(traceback.format_exc())
    print(traceback.format_exc())
