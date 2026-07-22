#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -Eeuo pipefail

on_error() {
  local status=$?
  printf 'zenpkg-streaming-test: %s:%s: status=%s command=%q\n' \
    "${BASH_SOURCE[1]:-${BASH_SOURCE[0]}}" "${BASH_LINENO[0]:-0}" \
    "$status" "$BASH_COMMAND" >&2
  exit "$status"
}
trap on_error ERR

ZENPKG="${1:?usage: zenpkg_streaming_test.sh ZENPKG OUT}"
OUT="${2:?usage: zenpkg_streaming_test.sh ZENPKG OUT}"

rm -rf "$OUT"
mkdir -p "$OUT"

python3 - "$OUT" <<'PY'
from pathlib import Path
import sys

out = Path(sys.argv[1])
large = bytearray(1024 * 1024)
large[0:4] = b"HEAD"
large[-512:-508] = b"koly"
large[-4:] = b"TAIL"
(out / "large.dmg").write_bytes(large)

boundary = bytearray(64 * 1024 + 1)
boundary[0:4] = b"HEAD"
boundary[-1] = 0xA5
(out / "boundary.dat").write_bytes(boundary)
PY

check_file() {
  local file="$1"
  local expected_bytes="$2"
  local expected_sampled="$3"
  local probe_log="$OUT/$(basename "$file").probe.log"
  local expected_sha
  local probe_sha
  local hash_sha

  expected_sha="$(sha256sum "$file" | awk '{print $1}')"
  "$ZENPKG" probe "$file" > "$probe_log"
  grep -Fqx "bytes: $expected_bytes" "$probe_log"
  grep -Fqx "sampled_bytes: $expected_sampled" "$probe_log"
  probe_sha="$(awk '/^sha256: / {print $2}' "$probe_log")"
  hash_sha="$("$ZENPKG" hash "$file")"
  test "$probe_sha" = "$expected_sha"
  test "$hash_sha" = "$expected_sha"
}

check_file "$OUT/large.dmg" 1048576 66048
check_file "$OUT/boundary.dat" 65537 65537

grep -Fqx 'format: apple-dmg' "$OUT/large.dmg.probe.log"
grep -Fqx 'confidence: signature' "$OUT/large.dmg.probe.log"
grep -Fqx 'format: unknown' "$OUT/boundary.dat.probe.log"

printf 'ZENPKG_STREAMING_TEST_OK cases=2 hash=full probe=head65536+tail512 overlap=none\n'
