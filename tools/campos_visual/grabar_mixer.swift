// grabar_mixer.swift — captura canales de una interfaz USB multicanal con CoreAudio, SIN perder muestras
// (ffmpeg/avfoundation pierde ~8 % con la Pioneer DJM-750MK2, y QuickTime/Chrome solo toman los canales 1-2,
//  que en la DJM van en silencio: la mezcla llega por los canales 3-12).
//
//   swiftc -O grabar_mixer.swift -o grabar_mixer
//   ./grabar_mixer mezcla.wav [segundos]        # graba hasta Ctrl+C o los segundos pedidos
//   ./grabar_mixer --stdout [rate]               # PCM s16 estéreo por stdout (lo usa puente_mixer.js)
// Variables: DISPOSITIVO=DJM  PAR=3,4
import AVFoundation
import CoreAudio
import Foundation

setvbuf(stdout, nil, _IOFBF, 1 << 16)
let nombre = (ProcessInfo.processInfo.environment["DISPOSITIVO"] ?? "DJM").lowercased()
let par = (ProcessInfo.processInfo.environment["PAR"] ?? "9,10").split(separator: ",").compactMap { Int($0) }
let cA = (par.first ?? 3) - 1, cB = (par.count > 1 ? par[1] : 4) - 1
let args = Array(CommandLine.arguments.dropFirst())
let aStdout = args.first == "--stdout"
let log: (String) -> Void = { FileHandle.standardError.write(($0 + "\n").data(using: .utf8)!) }

func buscar() -> AudioDeviceID? {
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size)
    var ids = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids)
    for id in ids {
        var na = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
        var cf: Unmanaged<CFString>?
        var s = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        if AudioObjectGetPropertyData(id, &na, 0, nil, &s, &cf) == 0, let n = cf?.takeRetainedValue() as String?, n.lowercased().contains(nombre) { return id }
    }
    return nil
}

guard var dev = buscar() else { log("No encuentro un dispositivo con \"\(nombre)\""); exit(1) }
let engine = AVAudioEngine()
let input = engine.inputNode
guard let au = input.audioUnit else { log("Sin audioUnit"); exit(1) }
let st = AudioUnitSetProperty(au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, UInt32(MemoryLayout<AudioDeviceID>.size))
if st != 0 { log("No pude fijar el dispositivo (\(st))"); exit(1) }
let fmt = input.inputFormat(forBus: 0)
log("Dispositivo fijado · \(fmt.channelCount) canales · \(Int(fmt.sampleRate)) Hz · canales \(cA + 1),\(cB + 1)")
guard Int(fmt.channelCount) > max(cA, cB) else { log("El dispositivo no tiene esos canales"); exit(1) }

let rate = fmt.sampleRate
var archivo: AVAudioFile?
var frames: Int64 = 0
var picos = [Float](repeating: 0, count: 64)
let limite: Int64? = (!aStdout && args.count > 1 ? Double(args[1]) : nil).map { Int64($0 * rate) }
if !aStdout {
    let ruta = args.first ?? "mezcla.wav"
    let out = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: rate, channels: 2, interleaved: true)!
    archivo = try? AVAudioFile(forWriting: URL(fileURLWithPath: ruta), settings: out.settings, commonFormat: .pcmFormatInt16, interleaved: true)
    if archivo == nil { log("No pude crear \(ruta)"); exit(1) }
}
let salida = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: rate, channels: 2, interleaved: true)!

func cerrar() { archivo = nil; log("Pico por canal: " + picos.prefix(Int(fmt.channelCount)).enumerated().map { "\($0.offset + 1)=" + String(format: "%.1f dB", 20 * log10(Double($0.element) + 1e-9)) }.joined(separator: " ")); fflush(stdout); log("Listo: \(frames) muestras = \(String(format: "%.2f", Double(frames) / rate)) s"); exit(0) }
signal(SIGINT) { _ in cerrar() }
signal(SIGTERM) { _ in cerrar() }
signal(SIGPIPE, SIG_IGN)

input.installTap(onBus: 0, bufferSize: 4096, format: fmt) { buf, _ in
    guard let ch = buf.floatChannelData else { return }
    let n = Int(buf.frameLength)
    for c in 0..<Int(buf.format.channelCount) { var m = picos[c]; let q = ch[c]; for i in 0..<n { let v = abs(q[i]); if v > m { m = v } }; picos[c] = m }
    let o = AVAudioPCMBuffer(pcmFormat: salida, frameCapacity: AVAudioFrameCount(n))!
    o.frameLength = AVAudioFrameCount(n)
    let p = o.int16ChannelData![0]
    let a = ch[cA], b = ch[cB]
    for i in 0..<n {
        p[2 * i] = Int16(max(-1, min(1, a[i])) * 32767)
        p[2 * i + 1] = Int16(max(-1, min(1, b[i])) * 32767)
    }
    if aStdout { fwrite(p, 2, n * 2, stdout); fflush(stdout) } else { try? archivo?.write(from: o) }
    frames += Int64(n)
    if let l = limite, frames >= l { cerrar() }
}
engine.prepare()
do { try engine.start() } catch { log("No pude iniciar el motor de audio: \(error)"); exit(1) }
log(aStdout ? "Enviando PCM s16 estéreo por stdout…" : "Grabando… (Ctrl+C para terminar)")
RunLoop.main.run()
