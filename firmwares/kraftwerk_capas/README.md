# kraftwerk_capas

"Mensch-Maschine": electro de máquina en Re menor, 120 BPM (100–140). Homenaje a la **estética** de Kraftwerk (sólo sonidos e ideas: ninguna melodía ni letra de nadie). Tres capas con **reloj propio** y las mismas reglas que `bruma_capas` y `dub_capas`.

| Regla fija | |
|---|---|
| BTN1 | siguiente capa (BAJO → RITMO → SONIDOS) |
| POT1 | volumen de la capa |
| BTN2 | STOP / PLAY de esa capa (al volver entra en fase, en el siguiente compás de otra capa que suene) |
| POT2 | tempo de esa capa (1 BPM por paso) |

| Capa | POT3 | POT4 | BTN3 | BTN4 | BTN5 |
|---|---|---|---|---|---|
| BAJO | corte del filtro | sierra (0 = sub puro, arriba = Minimoog) | secuencia (16) | slide | octava |
| RITMO | punch del bombo | metal (nivel del golpe) | groove (16) | relleno de toms | percusión on/off |
| SONIDOS | voz (tamaño de la boca del robot) | espacio | progresión (4) | **voz** (mantener = el robot canta) | modo (acordes / + secuencia A, B / **+ tema** / + tuyo / + aleatoria) |

Qué se tomó de la banda (investigado en la web) y dónde vive:

- **Minimoog + secuenciador Synthanorma / Intervallomat** (líneas repetitivas, cuantizadas): bajo de sub + sierra filtrada con 16 secuencias, y una secuencia aleatoria que siempre cae en tonos del acorde.
- **Pads de batería electrónica (Simmons) y "metal contra metal"**: bombo seco, caja y palmada electrónicas, toms con barrido de tono, golpe de metal con parciales inarmónicos, pistones de tren con el shaker. 16 grooves, sin swing.
- **Vocoder / habla sintética (Robovox, Votrax; "más robótico")**: voz de tres formantes, monótona, que canta vocales sobre la fundamental del acorde.
- **Teclados de cuerdas / Polymoog**: acordes simples y sostenidos.
- **Sonido seco, repetición y minimalismo**: casi sin reverb; el primer compás de cada frase sale exacto.

Progresiones: MONO (Dm) · EXPRESO (Dm–Bb) · AUTOBAHN (Dm–Bb–F–C) · MAQUINA (Dm–Gm–Dm–A).

- Pantalla OLED SSD1306 opcional (SDA 21 / SCL 38): anuncia la capa y muestra el mapa de perillas y botones.
- Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash Mode: DIO. Requiere FastLED.
- `-DDIAG=1` imprime por Serial0 la carga de CPU y el nivel (el USB de la placa es un puente UART).
- Medido en la placa (peor caso forzado: voz cantando y metal): 62–63 % de CPU promedio, 73–76 % máximo, 0 retrasos; RAM 67 %, flash 38 %.

## Sincronía y tema

- **Imán de tempo:** si dos capas quedan con el mismo BPM, la de número más alto se engancha sola al inicio del compás de la menor. Igualar los BPM basta para recuperar la sincronía.
- **TEMA** (modo por defecto): motivo original de 4 compases escrito en semitonos sobre el acorde en curso (sigue la progresión AUTOBAHN, que es la de arranque).
- **TUYO:** `TUYO_N[4]` en el `.ino` es una tabla vacía para tu propia transcripción (notas absolutas en semitonos sobre Re3, 0–9 y a–z). El firmware no incluye melodías protegidas de nadie.
