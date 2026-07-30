#!/usr/bin/env python3
"""Post-transform reliability and QEMU PC-speaker WAV evidence pass."""

from __future__ import annotations

from pathlib import Path


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"experience-audio-post: {label}: expected one match, got {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"experience-audio-post: {label}")


script = Path("tests/qemu_display_ui.sh")

replace_once(
    script,
    'rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img "$OUT"/*.txt\n\nSERIAL=',
    'rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img "$OUT"/*.txt "$OUT"/*.wav\n\nSERIAL=',
    "cleaned previous WAV evidence",
)

replace_once(
    script,
    'SERIAL="$(cd "$OUT" && pwd)/serial.log"\nRUNTIME_DATA=',
    'SERIAL="$(cd "$OUT" && pwd)/serial.log"\nAUDIO_WAV="$(cd "$OUT" && pwd)/system-sounds.wav"\nRUNTIME_DATA=',
    "declared absolute WAV evidence path",
)

replace_once(
    script,
    '''  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_SOUND_PREVIEW_OK" || { echo quit; return 1; }
''',
    '''  echo "sendkey up 10"
  sleep 0.08
  echo "sendkey ret 10"
  wait_for_serial "UI_SOUND_PREVIEW_OK" || { echo quit; return 1; }
''',
    "made preview focus navigation loss-resistant",
)

replace_once(
    script,
    '''  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \\
  -boot a -m 64M -machine pc,vmport=off -vga none -device VGA,vgamem_mb=64 -display none \\
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \\
''',
    '''  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \\
  -audiodev "wav,id=uiwav,path=$AUDIO_WAV" \\
  -boot a -m 64M -machine pc,vmport=off,pcspk-audiodev=uiwav -vga none -device VGA,vgamem_mb=64 -display none \\
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \\
''',
    "bound emulated PC speaker to QEMU WAV backend",
)

replace_once(
    script,
    '''fi

check_ppm() {
''',
    '''fi

[[ -s "$AUDIO_WAV" ]] || { echo "qemu-display-ui: missing PC speaker WAV evidence" >&2; exit 1; }
python3 - "$AUDIO_WAV" "$OUT/system-sound-evidence.txt" <<'PY'
from array import array
from pathlib import Path
import sys
import wave

wav_path = Path(sys.argv[1])
evidence_path = Path(sys.argv[2])
with wave.open(str(wav_path), "rb") as stream:
    channels = stream.getnchannels()
    width = stream.getsampwidth()
    rate = stream.getframerate()
    frames = stream.getnframes()
    raw = stream.readframes(frames)

if channels not in (1, 2) or width != 2 or rate < 8000 or frames <= 0:
    raise SystemExit(
        f"invalid PC speaker WAV: channels={channels} width={width} rate={rate} frames={frames}")

samples = array("h")
samples.frombytes(raw)
if sys.byteorder != "little":
    samples.byteswap()
absolute = [abs(sample) for sample in samples]
peak = max(absolute, default=0)
active = sum(value >= 64 for value in absolute)
active_frames = active // channels
active_ms = active_frames * 1000.0 / rate
 duration_seconds = frames / rate

if peak < 256 or active_frames < max(32, rate // 200):
    raise SystemExit(
        f"silent or truncated PC speaker WAV: peak={peak} active_frames={active_frames} rate={rate}")

text = (
    f"channels={channels}\n"
    f"sample_width_bytes={width}\n"
    f"sample_rate_hz={rate}\n"
    f"frames={frames}\n"
    f"duration_seconds={duration_seconds:.3f}\n"
    f"peak_abs_s16={peak}\n"
    f"active_frames={active_frames}\n"
    f"active_ms={active_ms:.3f}\n"
)
evidence_path.write_text(text, encoding="utf-8")
print(
    f"ui-system-sound-wav: OK duration={duration_seconds:.3f}s "
    f"active={active_ms:.3f}ms peak={peak}")
PY

check_ppm() {
'''.replace("\n duration_seconds", "\nduration_seconds"),
    "validated non-silent WAV payload and emitted measurements",
)

print("experience-audio-post: complete")
