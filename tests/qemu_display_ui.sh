#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-display-ui}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img

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
  sleep 0.45
}

should_capture() {
  case "$1" in
    640x480|960x540|1024x600|1024x768|1152x648|1280x720|1280x1024|1368x768|1440x900|1600x900|1600x1200) return 0 ;;
    *) return 1 ;;
  esac
}

controller() {
  wait_for_serial "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial "UI_ADAPTIVE_DISPLAY_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_MODE_COUNT 22" || { echo quit; return 1; }
  wait_for_serial "UI_SETTINGS_CONTROLS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_START_MENU_OK" || { echo quit; return 1; }
  wait_for_serial "UI_QUICK_SETTINGS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_PERSONALIZATION_OK" || { echo quit; return 1; }
  wait_for_serial "UI_TASKBAR_OK" || { echo quit; return 1; }
  wait_for_serial "UI_ANIMATION_MODEL_OK" || { echo quit; return 1; }
  wait_for_serial "UI_SYSTEM_CENTER_OK" || { echo quit; return 1; }
  wait_for_serial "UI_ACCESSIBILITY_OK" || { echo quit; return 1; }
  wait_for_serial "UI_START_SYSTEM_TOOLS_OK" || { echo quit; return 1; }
  wait_for_serial "UI_SEPTINUM_SHELL_OK" || { echo quit; return 1; }
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
    1600x900 1600x1200 640x480 720x480 800x600 960x540
    960x600 1024x576 1024x600
  )

  local mode
  for mode in "${modes[@]}"; do
    echo "sendkey f9 10"
    wait_for_serial "UI_DISPLAY_MODE_OK $mode" || { echo quit; return 1; }
    if should_capture "$mode"; then capture_mode "desktop-$mode"; fi
  done

  echo "sendkey f9 10"
  wait_for_count "UI_DISPLAY_MODE_OK 1024x768" 2 || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_CYCLE_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_PERSIST_OK" || { echo quit; return 1; }

  echo "sendkey f8 10"
  sleep 0.3
  capture_mode start-1024x768
  echo "sendkey s 10"
  echo "sendkey e 10"
  echo "sendkey t 10"
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
controller | timeout 190s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
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
check_ppm "$OUT/settings-1024x768.ppm" "1024 768"
check_ppm "$OUT/settings-style-1024x768.ppm" "1024 768"
check_ppm "$OUT/start-1024x768.ppm" "1024 768"
check_ppm "$OUT/start-search-1024x768.ppm" "1024 768"
check_ppm "$OUT/quick-settings-1024x768.ppm" "1024 768"
check_ppm "$OUT/system-center-1024x768.ppm" "1024 768"
check_ppm "$OUT/settings-accessibility-1024x768.ppm" "1024 768"
check_ppm "$OUT/high-contrast-1024x768.ppm" "1024 768"
check_ppm "$OUT/large-pointer-1024x768.ppm" "1024 768"
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

for screenshot in "$OUT"/*.ppm; do check_ppm_payload "$screenshot"; done
check_distinct "$OUT/desktop-1024x768.ppm" "$OUT/start-1024x768.ppm"
check_distinct "$OUT/start-1024x768.ppm" "$OUT/start-search-1024x768.ppm"
check_distinct "$OUT/desktop-1024x768.ppm" "$OUT/quick-settings-1024x768.ppm"
check_distinct "$OUT/settings-1024x768.ppm" "$OUT/settings-style-1024x768.ppm"
check_distinct "$OUT/settings-accessibility-1024x768.ppm" "$OUT/high-contrast-1024x768.ppm"
check_distinct "$OUT/high-contrast-1024x768.ppm" "$OUT/large-pointer-1024x768.ppm"
check_distinct "$OUT/packages-status-1024x768.ppm" "$OUT/security-status-1024x768.ppm"
sha256sum "$OUT"/*.ppm >"$OUT/framebuffer-sha256.txt"

for mode in \
  640x480 720x480 800x600 960x540 960x600 1024x576 1024x600 1024x768 \
  1152x648 1152x720 1152x864 1280x720 1280x768 1280x800 1280x960 1280x1024 \
  1360x768 1368x768 1440x900 1536x864 1600x900 1600x1200; do
  grep -q "UI_DISPLAY_MODE_OK $mode" "$SERIAL" || {
    echo "qemu-display-ui: missing verified mode: $mode" >&2
    exit 1
  }
done

for marker in UI_ADAPTIVE_DISPLAY_OK UI_HYBRID_SCALER_OK "UI_DISPLAY_MODE_COUNT 22" UI_SETTINGS_CONTROLS_OK UI_START_MENU_OK UI_QUICK_SETTINGS_OK UI_PERSONALIZATION_OK UI_TASKBAR_OK UI_ANIMATION_MODEL_OK UI_SYSTEM_CENTER_OK UI_ACCESSIBILITY_OK UI_START_SYSTEM_TOOLS_OK UI_SEPTINUM_SHELL_OK UI_FONT_ATLAS_OK UI_FONT_METRICS_OK UI_BORDER_STROKE_OK UI_CONFIG_PARSER_OK UI_EDGE_AWARE_SCALER_OK UI_TEXT_ELLIPSIS_OK UI_CLIPPING_SAFETY_OK UI_COLOR_MIX_OK UI_HIGH_CONTRAST_ON UI_LARGE_POINTER_ON UI_START_PACKAGES_OPEN_OK UI_START_SECURITY_OPEN_OK UI_DISPLAY_CYCLE_OK UI_DISPLAY_PERSIST_OK; do
  grep -q "$marker" "$SERIAL" || { echo "qemu-display-ui: missing marker: $marker" >&2; exit 1; }
done

printf 'qemu-display-ui: OK modes=22 shell=start+quick+system-center+accessibility+packages+security serial=%s screenshots=%s\n' "$SERIAL" "$OUT/*.ppm"
