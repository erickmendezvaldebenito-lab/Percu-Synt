# dub_capas

Dub / roots estilo sound system en Sol menor, 72 BPM (60–100). Tres capas con **reloj propio** y las mismas reglas que `bruma_capas`.

| Regla fija | |
|---|---|
| BTN1 | siguiente capa (BAJO → RITMO → SONIDOS) |
| POT1 | volumen de la capa |
| BTN2 | STOP / PLAY de esa capa (al volver entra en fase, en el siguiente compás de otra capa que suene) |
| POT2 | tempo de esa capa (1 BPM por paso) |

| Capa | POT3 | POT4 | BTN3 | BTN4 | BTN5 |
|---|---|---|---|---|---|
| BAJO | corte del cuerpo | cuerpo (0 = sub puro) | patrón (8) | slide | octava |
| RITMO | punch del bombo (cola y golpe) | hats | groove (8: one drop, rockers, steppers…) | relleno | percusión |
| SONIDOS | brillo del skank | espacio (eco + reverb) | progresión (4) | **eco infinito** (mantener) | modo (skank / melodía / burbuja) |

- Voces: sub de seno con notas largas y slides (cuerpo de sierra opcional), bombo redondo de cola larga afinado a la armonía, caja y rimshot, hats a contratiempo, skank de acordes secos, melodía y burbuja de órgano, eco de corchea que se oscurece en cada repetición y reverb de placa.
- Progresiones: UNO (Gm7) · ROOTS (Gm7–Cm7) · STEPPERS (Gm7–Cm7–Gm7–Dm7) · MISTICO (Gm7–Bbmaj7–Cm7–Dm7).
- Pantalla OLED SSD1306 opcional (SDA 21 / SCL 38): anuncia la capa y muestra el mapa de perillas y botones.
- Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash Mode: DIO. Requiere FastLED.
- `-DDIAG=1` imprime por Serial0 la carga de CPU y el nivel (el USB de la placa es un puente UART).
- Medido en la placa (peor caso): 52–56 % de CPU promedio, 76–77 % máximo, 0 retrasos; pico 0,45–0,68, compresor inactivo.
