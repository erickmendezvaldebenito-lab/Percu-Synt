#!/bin/bash
# iniciar_puente.command — doble clic: abre la Terminal y deja corriendo el puente de audio (mixer DJM y sonido del Mac).
cd "$(dirname "$0")" || exit 1
[ -x ./audio_sistema ] || swiftc -O audio_sistema.swift -o audio_sistema
[ -x ./grabar_mixer ]  || swiftc -O grabar_mixer.swift -o grabar_mixer
export FFMPEG="${FFMPEG:-$(command -v ffmpeg || echo /Users/erick/.local/bin/ffmpeg)}"
echo "Puente de audio en http://127.0.0.1:8765 — déjalo abierto. En la página pulsa 🖥 Audio del computador."
exec node puente_mixer.js
