#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
FIXTURES="${2:-build/qemu/zenpkg-blkdebug-fixtures}"
OUT="${3:-build/qemu/zenpkg-blkdebug}"
HMP_CLIENT="${4:-build/zenpkg-qmp-hmp-client}"
PROMPT='zenov> '
GUEST_COMMAND='pkg transport resume hello-native'
BLOCK_DEVICE='runtime-debug'

mkdir -p "$OUT"
rm -f "$OUT"/serial-*.log "$OUT"/qemu-*.stderr "$OUT"/qmp-*.sock \
  "$OUT"/qmp-*-arm.json "$OUT"/qmp-*-break.json "$OUT"/runtime-*.img "$OUT"/summary.log

VM_PID=''
cleanup_vm() {
  if [[ -n "$VM_PID" ]] && kill -0 "$VM_PID" 2>/dev/null; then
    kill -9 "$VM_PID" 2>/dev/null || true
    wait "$VM_PID" 2>/dev/null || true
  fi
  VM_PID=''
}
trap cleanup_vm EXIT

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-600}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "zenpkg-blkdebug: missing serial marker: $text" >&2
  return 1
}

wait_for_boot() {
  local serial="$1"
  local marker
  for marker in \
    ZENOVOS_BOOT_OK ZENOVFS_MOUNT_OK ZENOV_GUARD_READY \
    'ZENREPO_READY trust=verified packages=2' ZENPKG_SHA256_OK \
    ZENPKG_MANAGER_READY "$PROMPT"; do
    wait_for_serial "$serial" "$marker"
  done
}

hmp() {
  local socket="$1" command="$2" timeout="${3:-30}"
  "$HMP_CLIENT" "$socket" "$command" "$timeout"
}

require_hmp_success() {
  local response="$1" operation="$2"
  if grep -Eiq 'Could not|Cannot find|not found|No medium|Invalid argument|Permission denied|Error:' <<<"$response"; then
    echo "zenpkg-blkdebug: HMP $operation failed: $response" >&2
    return 1
  fi
}

send_text() {
  local socket="$1" text="$2" char lower key
  local i
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) key="$char" ;;
      [A-Z]) lower="${char,,}"; key="shift-$lower" ;;
      ' ') key=spc ;;
      '.') key=dot ;;
      '-') key=minus ;;
      '_') key=shift-minus ;;
      '/') key=slash ;;
      *) echo "zenpkg-blkdebug: unsupported key: $char" >&2; return 1 ;;
    esac
    hmp "$socket" "sendkey $key 10" 10 >/dev/null
    sleep 0.01
  done
}

send_command() {
  local socket="$1" command="$2"
  send_text "$socket" "$command"
  hmp "$socket" 'sendkey ret 10' 10 >/dev/null
}

start_qemu() {
  local runtime="$1" serial="$2" socket="$3" stderr="$4" mode="$5"
  rm -f "$serial" "$socket" "$stderr"
  if [[ "$mode" == fault ]]; then
    "$QEMU" \
      -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
      -blockdev "driver=file,node-name=runtime-file,filename=$runtime,cache.direct=off,cache.no-flush=off" \
      -blockdev 'driver=raw,node-name=runtime-raw,file=runtime-file' \
      -blockdev 'driver=blkdebug,node-name=runtime-debug,image=runtime-raw' \
      -device 'ide-hd,id=zenpkg-disk,drive=runtime-debug,bus=ide.0,unit=0' \
      -boot a -m 32M -machine pc,vmport=off -vga std -display none \
      -serial "file:$serial" -qmp "unix:$socket,server=on,wait=off" -monitor none \
      -no-reboot -no-shutdown >/dev/null 2>"$stderr" &
  else
    "$QEMU" \
      -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
      -drive "file=$runtime,format=raw,if=ide,index=0,media=disk,cache=none" \
      -boot a -m 32M -machine pc,vmport=off -vga std -display none \
      -serial "file:$serial" -qmp "unix:$socket,server=on,wait=off" -monitor none \
      -no-reboot -no-shutdown >/dev/null 2>"$stderr" &
  fi
  VM_PID=$!
}

stop_clean() {
  local socket="$1"
  hmp "$socket" quit 10 >/dev/null || true
  wait "$VM_PID"
  VM_PID=''
}

crash_now() {
  kill -9 "$VM_PID"
  local status=0
  wait "$VM_PID" || status=$?
  VM_PID=''
  [[ "$status" -eq 137 ]] || { echo "zenpkg-blkdebug: expected SIGKILL status 137, got $status" >&2; return 1; }
}

assert_clean_logs() {
  local serial="$1" stderr="$2" phase="$3"
  [[ ! -s "$stderr" ]] || { echo "zenpkg-blkdebug: non-empty QEMU stderr in $phase" >&2; cat "$stderr" >&2; return 1; }
  ! grep -Eq 'PANIC|ASSERT|DOUBLE FAULT|ZENPKG_TRANSPORT_RETRY_EXHAUSTED' "$serial" || {
    echo "zenpkg-blkdebug: forbidden marker in $phase" >&2; tail -n 160 "$serial" >&2; return 1;
  }
}

fault_boot() {
  local name="$1" runtime="$2" ordinal="$3"
  local serial="$OUT/serial-$name-fault.log" socket="$OUT/qmp-$name-fault.sock"
  local stderr="$OUT/qemu-$name-fault.stderr" evidence="$OUT/qmp-$name-break.json"
  local arm_evidence="$OUT/qmp-$name-arm.json"
  local tag="zenpkg-$name" hit response

  start_qemu "$runtime" "$serial" "$socket" "$stderr" fault
  wait_for_boot "$serial"
  response="$(hmp "$socket" "qemu-io $BLOCK_DEVICE \"break pwritev $tag\"" 15)"
  printf '%s\n' "$response" >"$arm_evidence"
  require_hmp_success "$response" 'break'
  printf 'ZENPKG_BLKDEBUG_STAGE name=%s stage=armed ordinal=%s\n' "$name" "$ordinal"

  send_command "$socket" "$GUEST_COMMAND"
  wait_for_serial "$serial" 'ZENPKG_TRANSPORT_RESUME name=hello-native version=0.2.0' 200
  printf 'ZENPKG_BLKDEBUG_STAGE name=%s stage=guest-command-running\n' "$name"

  for ((hit=1; hit<=ordinal; ++hit)); do
    response="$(hmp "$socket" "qemu-io $BLOCK_DEVICE \"wait_break $tag\"" 60)"
    require_hmp_success "$response" 'wait_break'
    printf 'ZENPKG_BLKDEBUG_STAGE name=%s stage=break-hit hit=%s\n' "$name" "$hit"
    if ((hit < ordinal)); then
      response="$(hmp "$socket" "qemu-io $BLOCK_DEVICE \"resume $tag\"" 15)"
      require_hmp_success "$response" 'resume'
      sleep 0.2
    fi
  done

  cat >"$evidence" <<EOF
{
  "block_device": "$BLOCK_DEVICE",
  "crash": "SIGKILL",
  "event": "pwritev",
  "guest_command": "$GUEST_COMMAND",
  "hit_count": $ordinal,
  "ordinal": $ordinal,
  "tag": "$tag"
}
EOF
  crash_now

  ! grep -Fq 'ZENPKG_CACHE_FETCH_COMMIT_OK' "$serial" || {
    echo "zenpkg-blkdebug: scenario committed before crash: $name" >&2; return 1;
  }
  assert_clean_logs "$serial" "$stderr" "fault-$name"
}

recovery_boot() {
  local name="$1" runtime="$2"
  local serial="$OUT/serial-$name-recovery.log" socket="$OUT/qmp-$name-recovery.sock"
  local stderr="$OUT/qemu-$name-recovery.stderr"

  start_qemu "$runtime" "$serial" "$socket" "$stderr" recovery
  wait_for_boot "$serial"
  send_command "$socket" "$GUEST_COMMAND"
  local i
  for ((i=0; i<600; ++i)); do
    if grep -Fq 'ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0' "$serial" || \
       grep -Fq 'ZENPKG_CACHE_HIT name=hello-native version=0.2.0' "$serial"; then
      break
    fi
    sleep 0.1
  done
  ((i < 600)) || { echo "zenpkg-blkdebug: recovery did not produce verified cache object: $name" >&2; return 1; }

  send_command "$socket" 'pkg cache verify'
  wait_for_serial "$serial" 'ZENPKG_CACHE_VERIFY_OK objects=1 partials=0'
  send_command "$socket" fsck
  wait_for_serial "$serial" ZENOVFS_FSCK_OK
  stop_clean "$socket"
  assert_clean_logs "$serial" "$stderr" "recovery-$name"
  ! grep -Fq ZENPKG_CACHE_INIT_REJECTED "$serial"
}

run_scenario() {
  local name="$1" fixture="$2" ordinal="$3"
  local runtime="$OUT/runtime-$name.img"
  cp "$FIXTURES/$fixture" "$runtime"
  cmp "$FIXTURES/$fixture" "$runtime"
  sync -f "$runtime"
  cmp "$FIXTURES/$fixture" "$runtime"
  sha256sum "$runtime"
  fault_boot "$name" "$runtime" "$ordinal"
  recovery_boot "$name" "$runtime"
  printf 'ZENPKG_BLKDEBUG_BREAKPOINT_SCENARIO_OK name=%s event=pwritev ordinal=%s crash=SIGKILL\n' \
    "$name" "$ordinal" | tee -a "$OUT/summary.log"
}

[[ -f "$BOOT_IMAGE" && -x "$HMP_CLIENT" ]] || { echo 'zenpkg-blkdebug: boot image and HMP client are required' >&2; exit 2; }
for fixture in resume.img ready.img committed.img; do
  [[ -f "$FIXTURES/$fixture" ]] || { echo "zenpkg-blkdebug: missing fixture $fixture" >&2; exit 2; }
done

run_scenario chunk-first-write resume.img 1
run_scenario chunk-second-write resume.img 2
run_scenario chunk-metadata-sync resume.img 8
run_scenario rename-first-write ready.img 1

printf 'ZENPKG_BLKDEBUG_LIVE_CRASHES_OK scenarios=4 qemu-boots=8\n'
