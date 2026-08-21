#!/usr/bin/env bash
# Play an audible alarm tone to notify the user of a required manual action
# (e.g. connect controller, unplug/replug USB). The tone is committed as
# notify_alarm.wav; regenerate with notify_gen.py if it goes missing.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WAV="$SCRIPT_DIR/notify_alarm.wav"

if [[ ! -f "$WAV" ]]; then
    echo "[notify] missing $WAV, run notify_gen.py to regenerate" >&2
    exit 1
fi

if command -v paplay >/dev/null 2>&1; then
    paplay "$WAV"
elif command -v ffplay >/dev/null 2>&1; then
    ffplay -nodisp -autoexit "$WAV" >/dev/null 2>&1
else
    echo "[notify] no audio player (paplay/ffplay) available" >&2
    exit 1
fi
