#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-display-ui}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img "$OUT"/*.txt

SERIAL="$(cd "$OUT" && pwd)/serial.log"
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-display-ui.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_serial() {
  local text="$1"
  for _ in $(seq 1 1200); do
    [[ -f "$SERIAL" ]] && grep -q "$text" "$SERIAL" && return 0
    sleep 0.1
  done
  return 1
}

wait_for_count() {
  local text="$1" expected="$2"
  for _ in $(seq 1 1200); do
    [[ -f "$SERIAL" ]] && [[ "$(grep -c "$text" "$SERIAL" || true)" -ge "$expected" ]] && return 0
    sleep 0.1
  done
  return 1
}

capture_mode() {
  local name="$1"
  local file="$(cd "$OUT" && pwd)/${name}.ppm"
  echo "screendump $file"
  sleep 0.9
}

should_capture() {
  case "$1" in
    640x480|960x540|1024x600|1024x768|1152x648|1280x720|1280x1024|1368x768|1440x900|1600x900|1600x1200|1920x1080|2048x1080|2560x1440|3840x2160|4096x2160) return 0 ;;
    *) return 1 ;;
  esac
}

controller() {
  wait_for_serial "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial "UI_ADAPTIVE_DISPLAY_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_MODE_COUNT 32" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_2K_CATALOG_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_4K_CATALOG_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_2K_READY" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_4K_READY" || { echo quit; return 1; }
  wait_for_serial "FRAMEBUFFER_VRAM_BYTES 67108864" || { echo quit; return 1; }
  wait_for_serial "FRAMEBUFFER_MAPPING_BYTES 67108864" || { echo quit; return 1; }
  wait_for_serial "FRAMEBUFFER_MAPPING_LARGE_PAGES" || { echo quit; return 1; }
  wait_for_serial "UI_SETTINGS_CONTROLS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_START_MENU_OK" || { echo quit; return 1; }
  wait_for_serial "UI_QUICK_SETTINGS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_PERSONALIZATION_OK" || { echo quit; return 1; }
  wait_for_serial "UI_TASKBAR_OK" || { echo quit; return 1; }
  wait_for_serial "UI_SYSTEM_CENTER_OK" || { echo quit; return 1; }
  wait_for_serial "UI_ACCESSIBILITY_OK" || { echo quit; return 1; }
  wait_for_serial "UI_START_SYSTEM_TOOLS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_SEPTINUM_SHELL_OK" || { echo quit; return 1; }
  wait_for_serial "UI_SEPTINUM_V2_OK" || { echo quit; return 1; }
  wait_for_serial "UI_LAUNCHER_MORPH_OK" || { echo quit; return 1; }
  wait_for_serial "UI_MINIMAL_SHELL_OK" || { echo quit; return 1; }
  wait_for_serial "UI_WIDESCREEN_CANVAS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_FONT_ATLAS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_FONT_METRICS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_BORDER_STROKE_OK" || { echo quit; return 1; }
  wait_for_serial "UI_CONFIG_PARSER_OK" || { echo quit; return 1; }
  wait_for_serial "UI_EDGE_AWARE_SCALER_OK" || { echo quit; return 1; }
  wait_for_serial "UI_TEXT_ELLIPSIS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_CLIPPING_SAFETY_OK" || { echo quit; return 1; }
  wait_for_serial "UI_COLOR_MIX_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_MODE_OK 1024x768" || { echo quit; return 1; }
  capture_mode desktop-1024x768

  local modes=(
    1152x648 1152x720 1152x864 1280x720 1280x768 1280x800
    1280x960 1280x1024 1360x768 1368x768 1440x900 1536x864
    1600x900 1600x1200 1920x1080 1920x1200 2048x1080 2048x1152
    2048x1536 2560x1080 2560x1440 2560x1600 3840x2160 4096x2160
    640x480 720x480 800x600 960x540 960x600 1024x576 1024x600
  )

  local mode
  for mode in "${modes[@]}"; do
    echo "sendkey f9 10"
    wait_for_serial "UI_DISPLAY_MODE_OK $mode" || { echo quit; return 1; }
    if should_capture "$mode"; then capture_mode "desktop-$mode"; fi
    if [[ "$mode" == "1920x1080" ]]; then
      local start_open_before close_frame_before
      start_open_before="$(grep -c 'UI_START_SURFACE_PRESENTED_OPEN' "$SERIAL" || true)"
      echo "sendkey f8 10"
      wait_for_count "UI_START_SURFACE_PRESENTED_OPEN" $((start_open_before + 1)) || { echo quit; return 1; }
      capture_mode start-1920x1080
      close_frame_before="$(grep -c 'UI_FRAME_PRESENTED' "$SERIAL" || true)"
      echo "sendkey esc 10"
      wait_for_count "UI_FRAME_PRESENTED" $((close_frame_before + 1)) || { echo quit; return 1; }
    elif [[ "$mode" == "2560x1440" ]]; then
      local control_open_before close_frame_before
      control_open_before="$(grep -c 'UI_CONTROL_CENTER_PRESENTED_OPEN' "$SERIAL" || true)"
      echo "sendkey f10 10"
      wait_for_count "UI_CONTROL_CENTER_PRESENTED_OPEN" $((control_open_before + 1)) || { echo quit; return 1; }
      capture_mode control-center-2560x1440
      close_frame_before="$(grep -c 'UI_FRAME_PRESENTED' "$SERIAL" || true)"
      echo "sendkey esc 10"
      wait_for_count "UI_FRAME_PRESENTED" $((close_frame_before + 1)) || { echo quit; return 1; }
    fi
  done

  echo "sendkey f9 10"
  wait_for_count "UI_DISPLAY_MODE_OK 1024x768" 2 || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_CYCLE_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_PERSIST_OK" || { echo quit; return 1; }

  echo "sendkey f8 10"
  wait_for_serial "UI_TRANSITION_FRAMES 4" || { echo quit; return 1; }
  sleep 0.3
  capture_mode start-1024x768
  echo "mouse_move 1 0"
  sleep 0.2
  capture_mode start-pointer-hover-1024x768
  echo "sendkey s"
  sleep 0.05
  echo "sendkey e"
  sleep 0.05
  echo "sendkey t"
  wait_for_serial "UI_START_FILTER_OK" || { echo quit; return 1; }
  sleep 0.2
  capture_mode start-search-1024x768
  echo "sendkey esc 10"
  sleep 0.15

  echo "sendkey f10 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_SETTINGS_PERSIST_OK" || { echo quit; return 1; }
  sleep 0.3
  capture_mode quick-settings-1024x768
  echo "sendkey esc 10"
  sleep 0.15

  echo "sendkey f11 10"
  sleep 0.3
  capture_mode system-center-1024x768
  echo "sendkey esc 10"
  sleep 0.15

  echo "sendkey f7 10"
  wait_for_serial "UI_KEYBOARD_NAV_OK" || { echo quit; return 1; }
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  sleep 0.3
  capture_mode settings-1024x768

  echo "sendkey right 10"
  wait_for_count "UI_DISPLAY_MODE_OK 1152x648" 2 || { echo quit; return 1; }
  echo "sendkey left 10"
  wait_for_count "UI_DISPLAY_MODE_OK 1024x768" 3 || { echo quit; return 1; }
  echo "sendkey end 10"
  sleep 0.3
  capture_mode settings-style-1024x768
  echo "sendkey ret 10"
  wait_for_count "UI_SETTINGS_PERSIST_OK" 2 || { echo quit; return 1; }

  echo "sendkey end 10"
  sleep 0.3
  capture_mode settings-accessibility-1024x768
  echo "sendkey ret 10"
  wait_for_serial "UI_HIGH_CONTRAST_ON" || { echo quit; return 1; }
  sleep 0.25
  capture_mode high-contrast-1024x768
  echo "sendkey tab 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_LARGE_POINTER_ON" || { echo quit; return 1; }
  echo "mouse_move 40 -40"
  sleep 0.25
  capture_mode large-pointer-1024x768

  echo "sendkey tab 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_REDUCED_MOTION_ON" || { echo quit; return 1; }
  echo "sendkey f8 10"
  wait_for_serial "UI_TRANSITION_FRAMES 1" || { echo quit; return 1; }
  sleep 0.2
  capture_mode reduced-motion-start-1024x768
  echo "sendkey esc 10"
  sleep 0.15

  echo "sendkey f8 10"
  echo "sendkey backspace 10"
  echo "sendkey backspace 10"
  echo "sendkey backspace 10"
  echo "sendkey p 10"
  echo "sendkey a 10"
  echo "sendkey c 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_START_PACKAGES_OPEN_OK" || { echo quit; return 1; }
  sleep 0.25
  capture_mode packages-status-1024x768

  echo "sendkey f8 10"
  echo "sendkey backspace 10"
  echo "sendkey backspace 10"
  echo "sendkey backspace 10"
  echo "sendkey s 10"
  echo "sendkey e 10"
  echo "sendkey c 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_START_SECURITY_OPEN_OK" || { echo quit; return 1; }
  sleep 0.25
  capture_mode security-status-1024x768
  echo quit
}

set +e
controller | timeout 600s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 64M -machine pc,vmport=off -vga none -device VGA,vgamem_mb=64 -display none \
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-display-ui: QEMU failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$SERIAL" >&2 || true
  exit 1
fi

check_ppm() {
  local file="$1" expected="$2" dimensions
  [[ -s "$file" ]] || { echo "qemu-display-ui: missing screenshot: $file" >&2; return 1; }
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  [[ "$dimensions" == "$expected" ]] || {
    echo "qemu-display-ui: wrong dimensions for $file: got '$dimensions', expected '$expected'" >&2
    return 1
  }
}

check_ppm "$OUT/desktop-640x480.ppm" "640 480"
check_ppm "$OUT/desktop-960x540.ppm" "960 540"
check_ppm "$OUT/desktop-1024x600.ppm" "1024 600"
check_ppm "$OUT/desktop-1024x768.ppm" "1024 768"
check_ppm "$OUT/desktop-1152x648.ppm" "1152 648"
check_ppm "$OUT/desktop-1280x720.ppm" "1280 720"
check_ppm "$OUT/desktop-1280x1024.ppm" "1280 1024"
check_ppm "$OUT/desktop-1368x768.ppm" "1368 768"
check_ppm "$OUT/desktop-1440x900.ppm" "1440 900"
check_ppm "$OUT/desktop-1600x900.ppm" "1600 900"
check_ppm "$OUT/desktop-1600x1200.ppm" "1600 1200"
check_ppm "$OUT/desktop-1920x1080.ppm" "1920 1080"
check_ppm "$OUT/start-1920x1080.ppm" "1920 1080"
check_ppm "$OUT/desktop-2048x1080.ppm" "2048 1080"
check_ppm "$OUT/desktop-2560x1440.ppm" "2560 1440"
check_ppm "$OUT/control-center-2560x1440.ppm" "2560 1440"
check_ppm "$OUT/desktop-3840x2160.ppm" "3840 2160"
check_ppm "$OUT/desktop-4096x2160.ppm" "4096 2160"
check_ppm "$OUT/settings-1024x768.ppm" "1024 768"
check_ppm "$OUT/settings-style-1024x768.ppm" "1024 768"
check_ppm "$OUT/start-1024x768.ppm" "1024 768"
check_ppm "$OUT/start-search-1024x768.ppm" "1024 768"
check_ppm "$OUT/start-pointer-hover-1024x768.ppm" "1024 768"
check_ppm "$OUT/quick-settings-1024x768.ppm" "1024 768"
check_ppm "$OUT/system-center-1024x768.ppm" "1024 768"
check_ppm "$OUT/settings-accessibility-1024x768.ppm" "1024 768"
check_ppm "$OUT/high-contrast-1024x768.ppm" "1024 768"
check_ppm "$OUT/large-pointer-1024x768.ppm" "1024 768"
check_ppm "$OUT/reduced-motion-start-1024x768.ppm" "1024 768"
check_ppm "$OUT/packages-status-1024x768.ppm" "1024 768"
check_ppm "$OUT/security-status-1024x768.ppm" "1024 768"


check_ppm_payload() {
  local file="$1" dimensions width_px height_px payload actual
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  read -r width_px height_px <<<"$dimensions"
  payload=$((width_px * height_px * 3))
  actual="$(wc -c <"$file")"
  (( actual > payload && actual < payload + 128 )) || {
    echo "qemu-display-ui: invalid PPM payload size for $file: bytes=$actual pixels=$payload" >&2
    return 1
  }
}

check_distinct() {
  local first="$1" second="$2"
  if cmp -s "$first" "$second"; then
    echo "qemu-display-ui: screenshots unexpectedly identical: $first $second" >&2
    return 1
  fi
}

python3 - "$OUT/theme-contrast-matrix.txt" <<'PY'
import math
import sys

output_path = sys.argv[1]

def rgb(r, g, b): return (r << 16) | (g << 8) | b

def mix(first, second, numerator, denominator):
    numerator = min(numerator, denominator)
    inverse = denominator - numerator
    return rgb(
        ((((first >> 16) & 255) * inverse) + (((second >> 16) & 255) * numerator)) // denominator,
        ((((first >> 8) & 255) * inverse) + (((second >> 8) & 255) * numerator)) // denominator,
        (((first & 255) * inverse) + ((second & 255) * numerator)) // denominator,
    )

def approx_channel(value):
    x = value * 257
    result = (3053 * x + 32767) // 65535 + 6822
    result = (result * x + 32767) // 65535 + 125
    return (result * x + 32767) // 65535

def approx_luminance(color):
    r = approx_channel((color >> 16) & 255)
    g = approx_channel((color >> 8) & 255)
    b = approx_channel(color & 255)
    return (r * 2126 + g * 7152 + b * 722 + 5000) // 10000

def approx_ratio(first, second):
    a, b = approx_luminance(first), approx_luminance(second)
    lighter, darker = max(a, b), min(a, b)
    return ((lighter + 500) * 100) // (darker + 500)

def exact_luminance(color):
    values = []
    for shift in (16, 8, 0):
        channel = ((color >> shift) & 255) / 255.0
        values.append(channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4)
    return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722

def exact_ratio(first, second):
    a, b = exact_luminance(first), exact_luminance(second)
    return (max(a, b) + 0.05) / (min(a, b) + 0.05)

def surface_min(theme, color, exact=False):
    ratio = exact_ratio if exact else lambda a, b: approx_ratio(a, b) / 100.0
    return min(ratio(color, theme[key]) for key in ('surface', 'surface_high', 'surface_low'))

def role_min(theme, color, focus, exact=False):
    minimum = surface_min(theme, color, exact)
    if focus:
        soft = mix(color, theme['surface'], 2, 3)
        ratio = exact_ratio(color, soft) if exact else approx_ratio(color, soft) / 100.0
        minimum = min(minimum, ratio)
    return minimum

def best_target(theme, focus):
    light, dark = rgb(255,255,255), rgb(0,0,0)
    return light if role_min(theme, light, focus) >= role_min(theme, dark, focus) else dark

def normalize(theme):
    theme = dict(theme)
    if surface_min(theme, theme['text']) < 4.5:
        theme['text'] = best_target(theme, False)
    if surface_min(theme, theme['muted']) < 4.5:
        candidate = mix(theme['muted'], theme['text'], 1, 2)
        theme['muted'] = candidate if surface_min(theme, candidate) >= 4.5 else theme['text']
    if surface_min(theme, theme['border']) < 3.0:
        candidate = mix(theme['border'], theme['text'], 1, 2)
        theme['border'] = candidate if surface_min(theme, candidate) >= 3.0 else theme['text']
    if role_min(theme, theme['accent'], True) < 3.0:
        target = best_target(theme, True)
        candidate = mix(theme['accent'], target, 1, 2)
        theme['accent'] = candidate if role_min(theme, candidate, True) >= 3.0 else target
    return theme

def base_theme(name):
    values = {
        'midnight': ((23,28,40),(31,37,52),(11,14,22),(54,62,82),(240,243,250),(153,163,187),(125,140,255)),
        'graphite': ((24,27,34),(34,38,47),(17,19,24),(57,63,75),(238,240,245),(151,158,172),(132,148,188)),
        'amber': ((38,27,25),(51,36,33),(27,20,19),(76,53,47),(246,235,224),(181,157,140),(222,155,84)),
    }[name]
    keys = ('surface','surface_high','surface_low','border','text','muted','accent')
    return {key: rgb(*value) for key, value in zip(keys, values)}

accents = [None, (104,146,255), (151,122,255), (68,191,184), (88,188,132), (229,126,118)]
rows = []
minimum_text, minimum_nontext = 99.0, 99.0
for theme_name in ('midnight','graphite','amber'):
    for slot, accent in enumerate(accents):
        theme = base_theme(theme_name)
        if accent is not None: theme['accent'] = rgb(*accent)
        theme = normalize(theme)
        text = min(surface_min(theme, theme['text'], True), surface_min(theme, theme['muted'], True))
        nontext = min(surface_min(theme, theme['border'], True), role_min(theme, theme['accent'], True, True))
        if text < 4.5 or nontext < 3.0:
            raise SystemExit(f'contrast matrix failed: {theme_name} slot={slot} text={text:.3f} nontext={nontext:.3f}')
        minimum_text, minimum_nontext = min(minimum_text, text), min(minimum_nontext, nontext)
        rows.append(f'{theme_name} accent_slot={slot} text={text:.3f} nontext={nontext:.3f}')

light = base_theme('midnight')
light.update(surface=rgb(246,247,250), surface_high=rgb(255,255,255), surface_low=rgb(222,225,232), accent=rgb(219,188,45))
light = normalize(light)
light_text = min(surface_min(light, light['text'], True), surface_min(light, light['muted'], True))
light_nontext = min(surface_min(light, light['border'], True), role_min(light, light['accent'], True, True))
if light_text < 4.5 or light_nontext < 3.0:
    raise SystemExit(f'light custom failed text={light_text:.3f} nontext={light_nontext:.3f}')
minimum_text, minimum_nontext = min(minimum_text, light_text), min(minimum_nontext, light_nontext)
rows.append(f'light-custom text={light_text:.3f} nontext={light_nontext:.3f}')

high = base_theme('amber')
high.update(surface=rgb(8,8,10), surface_high=rgb(28,28,32), surface_low=rgb(0,0,0),
            border=rgb(244,244,248), text=rgb(255,255,255), muted=rgb(218,218,224), accent=rgb(151,122,255))
high = normalize(high)
high_text = min(surface_min(high, high['text'], True), surface_min(high, high['muted'], True))
high_nontext = min(surface_min(high, high['border'], True), role_min(high, high['accent'], True, True))
if high_text < 4.5 or high_nontext < 3.0:
    raise SystemExit(f'high contrast failed text={high_text:.3f} nontext={high_nontext:.3f}')
minimum_text, minimum_nontext = min(minimum_text, high_text), min(minimum_nontext, high_nontext)
rows.append(f'high-contrast text={high_text:.3f} nontext={high_nontext:.3f}')
rows.append(f'matrix-minimum text={minimum_text:.3f} nontext={minimum_nontext:.3f}')
Path = __import__('pathlib').Path
Path(output_path).write_text('\n'.join(rows) + '\n', encoding='utf-8')
print(f'ui-contrast-matrix: OK cases={len(rows)-1} text_min={minimum_text:.3f} nontext_min={minimum_nontext:.3f}')
PY
python3 - "$OUT/display-mode-bytes.txt" <<'PY'
from pathlib import Path
import sys

modes = [
    (640,480),(720,480),(800,600),(960,540),(960,600),
    (1024,576),(1024,600),(1024,768),(1152,648),(1152,720),
    (1152,864),(1280,720),(1280,768),(1280,800),(1280,960),
    (1280,1024),(1360,768),(1368,768),(1440,900),(1536,864),
    (1600,900),(1600,1200),(1920,1080),(1920,1200),
    (2048,1080),(2048,1152),(2048,1536),(2560,1080),
    (2560,1440),(2560,1600),(3840,2160),(4096,2160),
]
rows = []
for width, height in modes:
    required = width * height * 4
    if required > 64 * 1024 * 1024:
        raise SystemExit(f'mode exceeds VGA memory: {width}x{height} bytes={required}')
    rows.append(f'{width}x{height} bytes={required}')
maximum = max(width * height * 4 for width, height in modes)
rows.append(f'maximum bytes={maximum}')
Path(sys.argv[1]).write_text('\n'.join(rows) + '\n', encoding='utf-8')
print(f'ui-display-byte-matrix: OK modes={len(modes)} maximum={maximum}')
PY
for screenshot in "$OUT"/*.ppm; do check_ppm_payload "$screenshot"; done
check_distinct "$OUT/desktop-1024x768.ppm" "$OUT/start-1024x768.ppm"
check_distinct "$OUT/start-1024x768.ppm" "$OUT/start-pointer-hover-1024x768.ppm"
check_distinct "$OUT/start-pointer-hover-1024x768.ppm" "$OUT/start-search-1024x768.ppm"
check_distinct "$OUT/desktop-1024x768.ppm" "$OUT/quick-settings-1024x768.ppm"
check_distinct "$OUT/settings-1024x768.ppm" "$OUT/settings-style-1024x768.ppm"
check_distinct "$OUT/settings-accessibility-1024x768.ppm" "$OUT/high-contrast-1024x768.ppm"
check_distinct "$OUT/high-contrast-1024x768.ppm" "$OUT/large-pointer-1024x768.ppm"
check_distinct "$OUT/large-pointer-1024x768.ppm" "$OUT/reduced-motion-start-1024x768.ppm"
check_distinct "$OUT/packages-status-1024x768.ppm" "$OUT/security-status-1024x768.ppm"
python3 - "$OUT" <<'PY'
from pathlib import Path
import sys

out = Path(sys.argv[1])

def ppm(name):
    data = (out / name).read_bytes()
    magic, dimensions, maximum, pixels = data.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    if magic != b'P6' or maximum != b'255' or len(pixels) != width * height * 3:
        raise SystemExit(f'invalid PPM: {name}')
    return width, height, pixels

def coverage(base_name, overlay_name, rect):
    width, height, base = ppm(base_name)
    ow, oh, overlay = ppm(overlay_name)
    if (width, height) != (ow, oh):
        raise SystemExit(f'dimension mismatch: {base_name} {overlay_name}')
    x, y, rw, rh = rect
    x0, x1 = x * width // 896, (x + rw) * width // 896
    y0, y1 = y * height // 504, (y + rh) * height // 504
    x0 += max(3, (x1 - x0) // 30)
    x1 -= max(3, (x1 - x0) // 30)
    y0 += max(3, (y1 - y0) // 40)
    y1 -= max(3, (y1 - y0) // 40)
    band_height = (y1 - y0) // 3
    result = []
    for band in range(3):
        by0 = y0 + band * band_height
        by1 = y1 if band == 2 else y0 + (band + 1) * band_height
        changed = total = 0
        for py in range(by0, by1):
            start, end = (py * width + x0) * 3, (py * width + x1) * 3
            first, second = base[start:end], overlay[start:end]
            total += len(first) // 3
            changed += sum(first[i:i+3] != second[i:i+3] for i in range(0, len(first), 3))
        ratio = changed / max(1, total)
        if changed < 1000 or ratio < 0.05:
            raise SystemExit(f'popup coverage failed: {overlay_name} band={band} changed={changed} ratio={ratio:.4f}')
        result.append((changed, ratio))
    return result

start = coverage('desktop-1920x1080.ppm', 'start-1920x1080.ppm', (208, 18, 480, 336))
control = coverage('desktop-2560x1440.ppm', 'control-center-2560x1440.ppm', (590, 244, 292, 190))
rows = [
    f"start bands={[v[0] for v in start]} ratios={[round(v[1], 4) for v in start]}",
    f"control-center bands={[v[0] for v in control]} ratios={[round(v[1], 4) for v in control]}",
]
(out / 'popup-vertical-coverage.txt').write_text('\n'.join(rows) + '\n', encoding='utf-8')
print('ui-popup-vertical-coverage: OK start=3/3 control-center=3/3')

width, height, full = ppm('start-1024x768.ppm')
_, _, filtered = ppm('start-search-1024x768.ppm')
viewport_y = 96
viewport_height = 576
side_segments = ((208, 236), (660, 688))

def difference_ratio(logical_y0, logical_y1):
    py0 = viewport_y + logical_y0 * viewport_height // 504
    py1 = viewport_y + logical_y1 * viewport_height // 504
    changed = total = 0
    for logical_x0, logical_x1 in side_segments:
        px0 = logical_x0 * width // 896
        px1 = logical_x1 * width // 896
        for py in range(py0, min(height, py1)):
            for px in range(px0, min(width, px1)):
                offset = (py * width + px) * 3
                total += 1
                if full[offset:offset+3] != filtered[offset:offset+3]:
                    changed += 1
    return changed, total, changed / max(1, total)

shared_changed, shared_total, shared_ratio = difference_ratio(18, 78)
morph_changed, morph_total, morph_ratio = difference_ratio(150, 330)
logical_height_delta = 336 - 126
physical_height_delta = logical_height_delta * viewport_height // 504
if morph_ratio < 0.80 or morph_ratio - shared_ratio < 0.30 or physical_height_delta < 200:
    raise SystemExit(
        f'launcher morph failed shared={shared_ratio:.4f} morph={morph_ratio:.4f} '
        f'physical_delta={physical_height_delta}')
(out / 'launcher-morph-evidence.txt').write_text(
    f'shared_changed={shared_changed}/{shared_total}\n'
    f'shared_ratio={shared_ratio:.4f}\n'
    f'morph_changed={morph_changed}/{morph_total}\n'
    f'morph_ratio={morph_ratio:.4f}\n'
    f'logical_height_delta={logical_height_delta}\n'
    f'physical_height_delta={physical_height_delta}\n',
    encoding='utf-8')
print(
    f'ui-launcher-morph-evidence: OK shared={shared_ratio:.4f} '
    f'morph={morph_ratio:.4f} physical_delta={physical_height_delta}')

desktop_width, desktop_height, desktop = ppm('desktop-1920x1080.ppm')
top_height = max(1, desktop_height * 7 // 100)
edges = 0
samples = 0
for py in range(top_height):
    for px in range(1, desktop_width):
        left = (py * desktop_width + px - 1) * 3
        right = left + 3
        if max(abs(desktop[left + channel] - desktop[right + channel]) for channel in range(3)) > 28:
            edges += 1
        samples += 1
for py in range(1, top_height):
    for px in range(desktop_width):
        above = ((py - 1) * desktop_width + px) * 3
        below = (py * desktop_width + px) * 3
        if max(abs(desktop[above + channel] - desktop[below + channel]) for channel in range(3)) > 28:
            edges += 1
        samples += 1
edge_ratio = edges / max(1, samples)
if edge_ratio >= 0.010:
    raise SystemExit(f'top chrome clutter failed edges={edges} ratio={edge_ratio:.5f}')
(out / 'chrome-density-evidence.txt').write_text(
    f'top_height={top_height}\nedges={edges}\nsamples={samples}\nratio={edge_ratio:.5f}\n',
    encoding='utf-8')
print(f'ui-chrome-density: OK ratio={edge_ratio:.5f}')
PY
sha256sum "$OUT"/*.ppm >"$OUT/framebuffer-sha256.txt"

for mode in \
  640x480 720x480 800x600 960x540 960x600 1024x576 1024x600 1024x768 \
  1152x648 1152x720 1152x864 1280x720 1280x768 1280x800 1280x960 1280x1024 \
  1360x768 1368x768 1440x900 1536x864 1600x900 1600x1200 \
  1920x1080 1920x1200 2048x1080 2048x1152 2048x1536 \
  2560x1080 2560x1440 2560x1600 3840x2160 4096x2160; do
  grep -q "UI_DISPLAY_MODE_OK $mode" "$SERIAL" || {
    echo "qemu-display-ui: missing verified mode: $mode" >&2
    exit 1
  }
done

for marker in UI_ADAPTIVE_DISPLAY_OK UI_HYBRID_SCALER_OK UI_WIDESCREEN_CANVAS_OK "UI_DISPLAY_MODE_COUNT 32" UI_DISPLAY_2K_CATALOG_OK UI_DISPLAY_4K_CATALOG_OK UI_DISPLAY_2K_READY UI_DISPLAY_4K_READY "FRAMEBUFFER_VRAM_BYTES 67108864" "FRAMEBUFFER_MAPPING_BYTES 67108864" FRAMEBUFFER_MAPPING_LARGE_PAGES UI_SETTINGS_CONTROLS_OK UI_START_MENU_OK UI_QUICK_SETTINGS_OK UI_PERSONALIZATION_OK UI_TASKBAR_OK UI_SYSTEM_CENTER_OK UI_ACCESSIBILITY_OK UI_START_SYSTEM_TOOLS_OK UI_SEPTINUM_SHELL_OK UI_SEPTINUM_V2_OK UI_LAUNCHER_MORPH_OK UI_MINIMAL_SHELL_OK UI_FONT_ATLAS_OK UI_FONT_METRICS_OK UI_BORDER_STROKE_OK UI_CONFIG_PARSER_OK UI_EDGE_AWARE_SCALER_OK UI_TEXT_ELLIPSIS_OK UI_CLIPPING_SAFETY_OK UI_COLOR_MIX_OK UI_REDUCED_MOTION_ON "UI_TRANSITION_FRAMES 4" "UI_TRANSITION_FRAMES 1" UI_START_FILTER_OK UI_HIGH_CONTRAST_ON UI_LARGE_POINTER_ON UI_START_PACKAGES_OPEN_OK UI_START_SECURITY_OPEN_OK UI_DISPLAY_CYCLE_OK UI_DISPLAY_PERSIST_OK; do
  grep -q "$marker" "$SERIAL" || { echo "qemu-display-ui: missing marker: $marker" >&2; exit 1; }
done

printf 'qemu-display-ui: OK modes=32 max=4096x2160 vram=64MiB shell=start+quick+system-center+accessibility+packages+security serial=%s screenshots=%s\n' "$SERIAL" "$OUT/*.ppm"
