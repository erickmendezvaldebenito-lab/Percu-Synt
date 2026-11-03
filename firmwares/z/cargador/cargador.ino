// ==============================================================================================================================================
// PERCU-SYNTH — Z · Cargador de la microSD por USB — GC Lab Chile
// ==============================================================================================================================================
// HARDWARE
// ==============================================================================================================================================
// - ESP32-S3 + módulo microSD (SPI): SCK 14 · MOSI 15 · MISO 16 · CS 17 · 3V3 · GND. Tarjeta en FAT32.
// ==============================================================================================================================================
// ARDUINO IDE
// ==============================================================================================================================================
// - Board: ESP32S3 Dev Module · USB CDC On Boot: Enabled · Flash Mode: DIO · Librerías: sólo las del core (SD, SPI)
// ==============================================================================================================================================
// DESCRIPCIÓN
// ==============================================================================================================================================
// Herramienta temporal: copia archivos del computador a la microSD SIN sacar la tarjeta del módulo. El computador (tools/z_prep/subir_a_sd.py)
// manda por el USB nativo (Serial, CDC) y la placa escribe a 20 MHz y devuelve el CRC32 de lo escrito (y puede releerlo del SD para verificarlo).
// Protocolo (texto, una orden por línea): HOLA · MKDIR ruta · PUT ruta bytes → "LISTO", luego bloques de 16 KB (la placa contesta 'A' a cada
// bloque) y al final "FIN crc bytes" · CRC ruta → "CRC crc bytes" · LS ruta · DEL ruta · DF. Sólo acepta rutas bajo /Z o /percusynth.
// ==============================================================================================================================================
#include <SD.h>
#include <SPI.h>

#define SD_SCK  14
#define SD_MISO 16
#define SD_MOSI 15
#define SD_CS   17
SPIClass sdSpi(FSPI);
static uint8_t buf[16384];
static uint32_t tabCrc[256];
static bool montado = false; static uint32_t frecOk = 0;

void iniciarCrc() { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); tabCrc[i] = c; } }
inline uint32_t crcAct(uint32_t c, const uint8_t *p, size_t n) { while (n--) c = tabCrc[(c ^ *p++) & 0xFF] ^ (c >> 8); return c; }

bool rutaValida(const String &r) {
  if (r.length() < 2 || r.length() > 120 || r.indexOf("..") >= 0) return false;
  return r.startsWith("/Z") || r.startsWith("/percusynth");
}
bool montar() {
  static const uint32_t FREC[3] = { 20000000, 10000000, 4000000 };
  for (int i = 0; i < 3; i++) {
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, sdSpi, FREC[i])) { frecOk = FREC[i]; return true; }
    SD.end(); sdSpi.end();
    delay(50);
  }
  return false;
}
void mkdirs(const String &ruta) {                                     // crea cada nivel de la ruta
  for (int i = 1; i <= (int)ruta.length(); i++)
    if (i == (int)ruta.length() || ruta[i] == '/') { String p = ruta.substring(0, i); if (!SD.exists(p)) SD.mkdir(p); }
}
void put(const String &ruta, uint32_t total) {
  int s = ruta.lastIndexOf('/'); if (s > 0) mkdirs(ruta.substring(0, s));
  if (SD.exists(ruta)) SD.remove(ruta);
  File f = SD.open(ruta, FILE_WRITE);
  if (!f) { Serial.println("ERR no abre"); return; }
  Serial.println("LISTO");
  uint32_t crc = 0xFFFFFFFFu, rec = 0; bool ok = true;
  while (rec < total) {
    size_t n = total - rec; if (n > sizeof(buf)) n = sizeof(buf);
    size_t got = Serial.readBytes((char *)buf, n);
    if (got != n) { ok = false; break; }
    if (f.write(buf, n) != n) { ok = false; break; }
    crc = crcAct(crc, buf, n); rec += n;
    Serial.write('A');
  }
  f.close();
  if (!ok) { Serial.printf("ERR recibidos %lu de %lu\n", (unsigned long)rec, (unsigned long)total); return; }
  Serial.printf("FIN %08lx %lu\n", (unsigned long)(crc ^ 0xFFFFFFFFu), (unsigned long)rec);
}
void crcArchivo(const String &ruta) {
  File f = SD.open(ruta); if (!f) { Serial.println("ERR no existe"); return; }
  uint32_t crc = 0xFFFFFFFFu, n = 0;
  while (f.available()) { int g = f.read(buf, sizeof(buf)); if (g <= 0) break; crc = crcAct(crc, buf, g); n += g; }
  f.close();
  Serial.printf("CRC %08lx %lu\n", (unsigned long)(crc ^ 0xFFFFFFFFu), (unsigned long)n);
}
void ls(const String &ruta) {
  File d = SD.open(ruta); if (!d || !d.isDirectory()) { Serial.println("ERR no es carpeta"); return; }
  for (File e = d.openNextFile(); e; e = d.openNextFile()) { Serial.printf("%s %s %lu\n", e.isDirectory() ? "D" : "F", e.name(), (unsigned long)e.size()); e.close(); }
  d.close(); Serial.println("FIN");
}
void setup() {
  iniciarCrc();
  Serial.setRxBufferSize(65536);
  Serial.begin(115200);
  Serial.setTimeout(8000);
  montado = montar();
}
bool asegurar() { if (!montado) montado = montar(); return montado; }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String l = Serial.readStringUntil('\n'); l.trim();
  if (l.length() == 0) return;
  if (l == "HOLA") {
    if (!asegurar()) { Serial.println("OK cargador SD=NO (la tarjeta no monta: revisa FAT32 y cables)"); return; }
    Serial.printf("OK cargador SD=%llu MB libres=%llu MB a %lu kHz\n", SD.totalBytes() / 1048576ULL, (SD.totalBytes() - SD.usedBytes()) / 1048576ULL, (unsigned long)(frecOk / 1000)); return; }
  if (l == "DF") { Serial.printf("DF total=%llu usado=%llu\n", SD.totalBytes(), SD.usedBytes()); return; }
  if (!asegurar()) { Serial.println("ERR sin tarjeta"); return; }
  int e = l.indexOf(' '); String cmd = e < 0 ? l : l.substring(0, e); String arg = e < 0 ? "" : l.substring(e + 1);
  if (cmd == "MKDIR") { if (!rutaValida(arg)) { Serial.println("ERR ruta"); return; } mkdirs(arg); Serial.println("OK"); }
  else if (cmd == "PUT") { int s = arg.lastIndexOf(' '); String r = arg.substring(0, s); uint32_t n = (uint32_t)arg.substring(s + 1).toInt();
                           if (s < 0 || !rutaValida(r)) { Serial.println("ERR ruta"); return; } put(r, n); }
  else if (cmd == "CRC") { if (!rutaValida(arg)) { Serial.println("ERR ruta"); return; } crcArchivo(arg); }
  else if (cmd == "LS")  { if (!rutaValida(arg)) { Serial.println("ERR ruta"); return; } ls(arg); }
  else if (cmd == "DEL") { if (!rutaValida(arg)) { Serial.println("ERR ruta"); return; } Serial.println(SD.remove(arg) ? "OK" : "ERR"); }
  else Serial.println("ERR orden");
}
