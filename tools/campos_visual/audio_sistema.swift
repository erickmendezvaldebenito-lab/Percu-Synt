// audio_sistema.swift — captura el SONIDO DEL SISTEMA (lo que sale del Mac: Spotify, Apple Music, YouTube…) con ScreenCaptureKit, SIN el cuadro
// "Compartiendo en esta pestaña" de Chrome. Entrega PCM s16 estéreo 48 kHz por stdout (lo usa puente_mixer.js en /sistema).
//   swiftc -O audio_sistema.swift -o audio_sistema
//   ./audio_sistema --stdout
// Requiere macOS 13+ y el permiso "Grabación de pantalla y audio del sistema" para la app desde la que lo lances (Terminal): macOS lo pide la 1.ª vez.
import Foundation
import ScreenCaptureKit
import CoreMedia
import AVFoundation
import CoreGraphics

setvbuf(stdout, nil, _IOFBF, 1 << 16)
signal(SIGPIPE, SIG_IGN)
let log: (String) -> Void = { FileHandle.standardError.write(($0 + "\n").data(using: .utf8)!) }

final class Salida: NSObject, SCStreamOutput, SCStreamDelegate {
    var muestras: Int64 = 0
    var buffers: Int64 = 0, pantallas: Int64 = 0
    func stream(_ stream: SCStream, didOutputSampleBuffer sb: CMSampleBuffer, of type: SCStreamOutputType) {
        if type == .screen { pantallas += 1 }
        guard type == .audio, sb.isValid, CMSampleBufferGetNumSamples(sb) > 0 else { return }
        buffers += 1
        do {
            try sb.withAudioBufferList { abl, _ in
                let bufs = UnsafeMutableAudioBufferListPointer(abl.unsafeMutablePointer)
                let n = Int(CMSampleBufferGetNumSamples(sb))
                var out = [Int16](repeating: 0, count: n * 2)
                func s16(_ v: Float) -> Int16 { Int16(max(-1, min(1, v)) * 32767) }
                if bufs.count >= 2 {                                   // planar: un buffer por canal
                    let l = bufs[0].mData!.assumingMemoryBound(to: Float.self), r = bufs[1].mData!.assumingMemoryBound(to: Float.self)
                    for i in 0..<n { out[2 * i] = s16(l[i]); out[2 * i + 1] = s16(r[i]) }
                } else if bufs.count == 1 {                            // intercalado
                    let ch = max(1, Int(bufs[0].mNumberChannels)), p = bufs[0].mData!.assumingMemoryBound(to: Float.self)
                    for i in 0..<n { out[2 * i] = s16(p[i * ch]); out[2 * i + 1] = s16(p[i * ch + (ch > 1 ? 1 : 0)]) }
                }
                out.withUnsafeBytes { _ = fwrite($0.baseAddress, 1, $0.count, stdout) }
                fflush(stdout)
            }
            muestras += Int64(CMSampleBufferGetNumSamples(sb))
        } catch { log("buffer de audio: \(error)") }
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) { log("captura detenida: \(error)"); exit(1) }
}

let salida = Salida()
var streamRef: SCStream?   // referencia fuerte: si el SCStream se libera al terminar la Task, la captura se corta tras el primer buffer
log("Permiso de grabación de pantalla (CGPreflightScreenCaptureAccess): \(CGPreflightScreenCaptureAccess())")
if !CGPreflightScreenCaptureAccess() { _ = CGRequestScreenCaptureAccess() }   // pide el permiso si aún no existe
Task {
    do {
        let contenido = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let pantalla = contenido.displays.first else { log("No hay pantalla"); exit(1) }
        let filtro = SCContentFilter(display: pantalla, excludingWindows: [])
        let cfg = SCStreamConfiguration()
        cfg.capturesAudio = true; cfg.excludesCurrentProcessAudio = (ProcessInfo.processInfo.environment["AS_INCLUIR"] == nil)
        cfg.sampleRate = 48000; cfg.channelCount = 2
        cfg.width = 2; cfg.height = 2; cfg.minimumFrameInterval = CMTime(value: 1, timescale: 2)   // el video no se usa: mínimo posible
        let stream = SCStream(filter: filtro, configuration: cfg, delegate: salida)
        let cola = DispatchQueue(label: "audio")
        try stream.addStreamOutput(salida, type: .audio, sampleHandlerQueue: cola)
        try stream.addStreamOutput(salida, type: .screen, sampleHandlerQueue: cola)
        streamRef = stream
        try await stream.startCapture()
        log("Audio del sistema · 48000 Hz · 2 canales")
    } catch {
        log("No pude capturar el audio del sistema: \(error)")
        log("→ Ajustes del Sistema → Privacidad y seguridad → Grabación de pantalla y audio del sistema: activa la app desde la que lo ejecutas (Terminal).")
        exit(1)
    }
}
if ProcessInfo.processInfo.environment["AS_DIAG"] != nil { Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in log("diag: buffers de audio \(salida.buffers) · muestras \(salida.muestras) · cuadros de pantalla \(salida.pantallas)") } }
RunLoop.main.run()
