#!/usr/bin/env node
/* =============================================================================
 PUENTE MIXER — manda al visualizador el audio de una mezcladora USB multicanal
 =============================================================================
 Por qué existe: Chrome solo captura 2 canales de una interfaz USB (los canales 1-2).
 La Pioneer DJM-750MK2 entrega 12 canales y, medido con ffmpeg, la mezcla llega por los
 canales 3-12 mientras los 1-2 están en silencio, así que Chrome "escucha" ceros.
 Este puente usa ffmpeg para tomar el par de canales correcto y lo sirve por HTTP local.

 USO:   node puente_mixer.js            (deja la ventana abierta)
        DISPOSITIVO=DJM PAR=3,4 PORT=8765 node puente_mixer.js
 Luego en la página: botón "🔌 Mixer (puente)".
 Requiere ffmpeg (brew install ffmpeg) y permiso de micrófono para la Terminal.
============================================================================= */
const http = require('http');
const { spawn, spawnSync } = require('child_process');

const PORT = +process.env.PORT || 8765;
const NOMBRE = (process.env.DISPOSITIVO || 'DJM').toLowerCase();
const [A, B] = (process.env.PAR || '9,10').split(',').map(Number);
const FFMPEG = process.env.FFMPEG || 'ffmpeg';

function buscarDispositivo() {
  const r = spawnSync(FFMPEG, ['-hide_banner', '-f', 'avfoundation', '-list_devices', 'true', '-i', ''], { encoding: 'utf8' });
  const txt = (r.stderr || '') + (r.stdout || '');
  const i = txt.indexOf('audio devices');
  if (i < 0) return null;
  for (const l of txt.slice(i).split('\n')) {
    const m = l.match(/\[(\d+)\]\s+(.*)$/);
    if (m && m[2].toLowerCase().includes(NOMBRE)) return { idx: m[1], nombre: m[2].trim() };
  }
  return null;
}

const disp = buscarDispositivo();
if (!disp) console.error('(aviso) No encuentro un dispositivo con "' + NOMBRE + '": /audio no funcionará, pero /sistema sí.');
else console.log('Dispositivo: [' + disp.idx + '] ' + disp.nombre + ' · canales ' + A + ',' + B + ' → http://127.0.0.1:' + PORT + '/audio');

const fs = require('fs'), path = require('path');
const CAPTURA = path.join(__dirname, 'grabar_mixer');           // CoreAudio sin pérdidas (swiftc -O grabar_mixer.swift -o grabar_mixer)
const usaSwift = fs.existsSync(CAPTURA);
const SISTEMA = path.join(__dirname, 'audio_sistema');   // ScreenCaptureKit: el sonido de todo el Mac, sin cuadro de Chrome (swiftc -O audio_sistema.swift -o audio_sistema)
const haySistema = fs.existsSync(SISTEMA);
console.log(usaSwift ? 'Captura: grabar_mixer (CoreAudio, sin pérdidas)' : 'Captura: ffmpeg (pierde ~8 % de muestras: compila grabar_mixer.swift)');

// --- audio del sistema: UN solo proceso de captura que se queda vivo y se reparte entre los clientes (arranque instantáneo: ScreenCaptureKit tarda varios segundos en abrir) ---
let sis = null, sisListo = false, sisParar = null;
const sisClientes = new Set(), sisPend = new Set();
function sisCabecera(res, cors) { try { res.writeHead(200, { ...cors, 'Content-Type': 'application/octet-stream', 'X-Rate': '48000' }); res.flushHeaders(); sisClientes.add(res); } catch (e) {} }   // flushHeaders: sin él, en silencio el cliente se queda esperando la cabecera
function sistemaCliente(res, cors) {
  clearTimeout(sisParar);
  if (!sis) {
    sis = spawn(SISTEMA, ['--stdout'], { stdio: ['ignore', 'pipe', 'pipe'] }); sisListo = false;
    sis.stderr.on('data', d => { process.stderr.write(d); if (!sisListo && /48000 Hz/.test(String(d))) { sisListo = true; for (const r of sisPend) sisCabecera(r, cors); sisPend.clear(); } });
    sis.stdout.on('data', b => { for (const r of sisClientes) { try { r.write(b); } catch (e) {} } });
    sis.on('exit', () => { sis = null; sisListo = false; for (const r of [...sisPend, ...sisClientes]) { try { if (!r.headersSent) r.writeHead(503, cors); r.end(); } catch (e) {} } sisPend.clear(); sisClientes.clear(); });
    setTimeout(() => { if (!sisListo) { for (const r of sisPend) { try { r.writeHead(503, cors); r.end(); } catch (e) {} } sisPend.clear(); } }, 20000);
  }
  if (sisListo) sisCabecera(res, cors); else sisPend.add(res);
  const salir = () => { sisClientes.delete(res); sisPend.delete(res); if (!sisClientes.size && !sisPend.size) sisParar = setTimeout(() => { try { sis && sis.kill('SIGKILL'); } catch (e) {} }, 60000); };
  res.on('close', salir);
  console.log('Cliente /sistema');
}

process.on('uncaughtException', e => console.error('Error:', e.message));   // el puente no se cae por un cliente
let actual = null;   // una sola captura a la vez: el dispositivo no se comparte
http.createServer((req, res) => {
  const cors = { 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store', 'Access-Control-Expose-Headers': 'X-Rate' };
  // La página también se sirve desde aquí (http://127.0.0.1:PUERTO/): mismo origen que el puente, así Chrome no bloquea nada (file:// → localhost puede pedir permiso de red local).
  const ruta = req.url.split('?')[0];
  const ESTATICOS = { '/': ['index.html', 'text/html; charset=utf-8'], '/index.html': ['index.html', 'text/html; charset=utf-8'], '/mp4-muxer.js': ['mp4-muxer.js', 'text/javascript; charset=utf-8'] };
  if (req.method === 'GET' && ESTATICOS[ruta]) {
    try { const [f, tipo] = ESTATICOS[ruta]; const b = fs.readFileSync(path.join(__dirname, f)); res.writeHead(200, { 'Content-Type': tipo, 'Cache-Control': 'no-store' }); res.end(b); }
    catch (e) { res.writeHead(404); res.end(); }
    return;
  }
  if (req.url.startsWith('/estado')) { res.writeHead(200, { ...cors, 'Content-Type': 'application/json' }); res.end(JSON.stringify({ ok: true, dispositivo: disp ? disp.nombre : null, par: [A, B], captura: usaSwift ? 'coreaudio' : 'ffmpeg', sistema: haySistema })); return; }
  if (req.url.startsWith('/sistema')) {
    if (!haySistema) { res.writeHead(503, cors); res.end('falta compilar audio_sistema'); return; }
    sistemaCliente(res, cors); return;
  }
  if (!req.url.startsWith('/audio')) { res.writeHead(404, cors); res.end(); return; }
  if (!disp) { res.writeHead(503, cors); res.end('sin mezcladora'); return; }
  const pedido = Math.min(96000, Math.max(8000, +(new URL(req.url, 'http://x').searchParams.get('rate')) || 44100));
  if (actual) { actual(); actual = null; }
  let proc, enviado = false, plazo;
  const parar = () => { try { proc && proc.kill('SIGKILL'); } catch (e) {} };
  const empezar = (rate) => {
    if (enviado || res.headersSent || res.destroyed) return; enviado = true; clearTimeout(plazo);
    res.writeHead(200, { ...cors, 'Content-Type': 'application/octet-stream', 'X-Rate': String(rate) }); res.flushHeaders();
    proc.stdout.pipe(res);
  };
  if (usaSwift) {
    proc = spawn(CAPTURA, ['--stdout'], { stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, DISPOSITIVO: NOMBRE, PAR: A + ',' + B } });
    let err = '';
    proc.stderr.on('data', d => { err += d; const m = err.match(/(\d+) Hz/); if (m) empezar(+m[1]); process.stderr.write(d); });
    plazo = setTimeout(() => empezar(48000), 4000);
  } else {
    proc = spawn(FFMPEG, ['-hide_banner', '-loglevel', 'error', '-f', 'avfoundation', '-i', ':' + disp.idx,
      '-af', 'pan=stereo|c0=c' + (A - 1) + '|c1=c' + (B - 1), '-ar', String(pedido), '-ac', '2', '-f', 's16le', '-'], { stdio: ['ignore', 'pipe', 'inherit'] });
    empezar(pedido);
  }
  actual = () => { parar(); try { res.end(); } catch (e) {} };
  res.on('close', parar); req.on('close', parar); proc.on('exit', () => { clearTimeout(plazo); try { res.end(); } catch (e) {} });
  console.log('Cliente conectado');
}).listen(PORT, '127.0.0.1');
