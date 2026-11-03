# bruma_capas

Downtempo / trip-hop en Mi menor, 84 BPM (70–110). Tres capas con **reloj propio** y las mismas reglas en todas.

| Regla fija | |
|---|---|
| BTN1 | siguiente capa (BAJO → RITMO → SONIDOS) |
| POT1 | volumen de la capa |
| BTN2 | STOP / PLAY de esa capa (al volver entra en fase, en el siguiente compás de otra capa que suene) |
| POT2 | tempo de esa capa (1 BPM por paso) |

| Capa | POT3 | POT4 | BTN3 | BTN4 | BTN5 |
|---|---|---|---|---|---|
| BAJO | corte del cuerpo | cuerpo (0 = sub puro) | patrón (8) | slide | octava |
| RITMO | punch del bombo | hats | groove (8) | relleno | percusión |
| SONIDOS | brillo del piano | espacio | progresión (4) | textura (al soltar, impacto) | modo (acordes / +arpegio / …) |

- Voces: sub con notas largas y slides (+ cuerpo de sierra opcional), batería con swing, piano eléctrico de acordes con novena, arpegio sincopado, eco de corchea y reverb de placa.
- Pantalla OLED SSD1306 opcional (SDA 21 / SCL 38): anuncia la capa y muestra el mapa de perillas y botones.
- Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash Mode: DIO. Requiere FastLED.
- `-DDIAG=1` imprime por Serial0 la carga de CPU y el nivel (el USB de la placa es un puente UART).
- Medido en la placa (peor caso): 51–60 % de CPU promedio, 80–83 % máximo, 0 retrasos; pico 0,62–0,74, compresor inactivo.
