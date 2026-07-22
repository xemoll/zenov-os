#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

ZENPKG="${1:?usage: zenpkg_foreign_test.sh ZENPKG HELLO.ZEX OUT}"
NATIVE_ZEX="${2:?usage: zenpkg_foreign_test.sh ZENPKG HELLO.ZEX OUT}"
OUT="${3:?usage: zenpkg_foreign_test.sh ZENPKG HELLO.ZEX OUT}"

rm -rf "$OUT"
mkdir -p "$OUT/fixtures"

python3 - "$OUT/fixtures" <<'PY'
from pathlib import Path
import struct
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

pe = bytearray(68)
pe[0:2] = b"MZ"
struct.pack_into("<I", pe, 0x3C, 0x40)
pe[0x40:0x44] = b"PE\0\0"
(out / "sample.exe").write_bytes(pe)
(out / "bare-mz.exe").write_bytes(b"MZ" + bytes(62))
(out / "sample.msi").write_bytes(bytes.fromhex("d0cf11e0a1b11ae1") + bytes(56))
(out / "sample.msix").write_bytes(b"PK\x03\x04" + bytes(60))
(out / "sample.deb").write_bytes(b"!<arch>\n" + bytes(56))
(out / "sample.rpm").write_bytes(bytes.fromhex("edabeedb") + bytes(60))
(out / "sample.pkg").write_bytes(b"xar!" + bytes(60))
(out / "sample.xvc").write_bytes(b"XVC fixture")
(out / "sample-playstation.pkg").write_bytes(b"\x7fCNT" + bytes(60))
(out / "sample.AppImage").write_bytes(b"\x7fELF" + bytes(60))
(out / "java-class.bin").write_bytes(bytes.fromhex("cafebabe0000003d"))
(out / "unknown.bin").write_bytes(b"unknown fixture")

# ELF32/i386 ET_EXEC with one PT_INTERP entry. The importer must reject it
# before any package is produced.
elf = bytearray(84)
elf[0:7] = b"\x7fELF\x01\x01\x01"
struct.pack_into("<HHIIIIIHHHHHH", elf, 16,
                 2, 3, 1, 0x1000, 52, 0, 0, 52, 32, 1, 0, 0, 0)
struct.pack_into("<IIIIIIII", elf, 52, 3, 0, 0, 0, 0, 0, 4, 1)
(out / "dynamic.elf").write_bytes(elf)
PY

probe_expect() {
  local file="$1" format="$2" support="$3" confidence="$4" log="$OUT/$(basename "$file").probe.log"
  "$ZENPKG" probe "$file" > "$log"
  grep -Fqx "format: $format" "$log"
  grep -Fqx "support: $support" "$log"
  grep -Fqx "confidence: $confidence" "$log"
  grep -Fq "PROBE_OK format=$format support=$support confidence=$confidence" "$log"
}

probe_expect "$NATIVE_ZEX" zex1 host-import signature
probe_expect "$OUT/fixtures/sample.exe" pe runtime-required signature
probe_expect "$OUT/fixtures/bare-mz.exe" unknown unsupported signature
probe_expect "$OUT/fixtures/sample.msi" msi runtime-required signature
probe_expect "$OUT/fixtures/sample.msix" msix runtime-required signature
probe_expect "$OUT/fixtures/sample.deb" deb inspect-only signature
probe_expect "$OUT/fixtures/sample.rpm" rpm inspect-only signature
probe_expect "$OUT/fixtures/sample.pkg" apple-pkg runtime-required signature
probe_expect "$OUT/fixtures/sample.xvc" xbox-xvc partner-only extension-only
probe_expect "$OUT/fixtures/sample-playstation.pkg" playstation-pkg partner-only signature
probe_expect "$OUT/fixtures/sample.AppImage" appimage runtime-required signature
probe_expect "$OUT/fixtures/java-class.bin" unknown unsupported signature
probe_expect "$OUT/fixtures/unknown.bin" unknown unsupported signature

echo ZENPKG_FOREIGN_PROBE_OK cases=13

import_once() {
  local output="$1" log="$2"
  "$ZENPKG" import-native "$NATIVE_ZEX" \
    --name imported-hello \
    --version 0.1.0 \
    --license BSD-2-Clause \
    --source packages/examples/hello-native \
    --asset-policy redistributable \
    --output "$output" > "$log"
  grep -Fq 'IMPORT_NATIVE_OK imported-hello@0.1.0 payload=zex1' "$log"
}

import_once "$OUT/import-a.zpk" "$OUT/import-a.log"
import_once "$OUT/import-b.zpk" "$OUT/import-b.log"
cmp "$OUT/import-a.zpk" "$OUT/import-b.zpk"
"$ZENPKG" verify "$OUT/import-a.zpk" | grep -Fq 'VERIFY_OK imported-hello@0.1.0'
"$ZENPKG" inspect "$OUT/import-a.zpk" > "$OUT/import.inspect.log"
grep -Fqx 'runtime: native' "$OUT/import.inspect.log"
grep -Fqx 'payload_type: zex1' "$OUT/import.inspect.log"
grep -Fqx 'entrypoint: /data/apps/pkg-imported-hello-0.1.0.zex' "$OUT/import.inspect.log"
grep -Fqx 'asset_policy: redistributable' "$OUT/import.inspect.log"
probe_expect "$OUT/import-a.zpk" zenpkg installable signature

echo ZENPKG_NATIVE_IMPORT_OK deterministic=1

set +e
"$ZENPKG" import-native "$OUT/fixtures/sample.exe" \
  --name rejected-pe --version 0.1.0 --license Proprietary \
  --source local-fixture --asset-policy redistributable \
  --output "$OUT/rejected-pe.zpk" > "$OUT/rejected-pe.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -Fq 'format pe is not eligible for native import; support=runtime-required' "$OUT/rejected-pe.log"
test ! -e "$OUT/rejected-pe.zpk"

set +e
"$ZENPKG" import-native "$OUT/fixtures/dynamic.elf" \
  --name rejected-elf --version 0.1.0 --license BSD-2-Clause \
  --source local-fixture --asset-policy redistributable \
  --output "$OUT/rejected-elf.zpk" > "$OUT/rejected-elf.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -Fq 'dynamic ELF and PT_INTERP executables cannot be imported' "$OUT/rejected-elf.log"
test ! -e "$OUT/rejected-elf.zpk"

cp "$NATIVE_ZEX" "$OUT/corrupt.zex"
printf '\x00' | dd of="$OUT/corrupt.zex" bs=1 seek=$(( $(stat -c%s "$OUT/corrupt.zex") - 1 )) conv=notrunc status=none
set +e
"$ZENPKG" import-native "$OUT/corrupt.zex" \
  --name corrupt-zex --version 0.1.0 --license BSD-2-Clause \
  --source local-fixture --asset-policy redistributable \
  --output "$OUT/corrupt.zpk" > "$OUT/corrupt.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -Fq 'ZEX1 image checksum mismatch' "$OUT/corrupt.log"
test ! -e "$OUT/corrupt.zpk"

echo ZENPKG_FOREIGN_REJECTION_OK cases=3
echo ZENPKG_FOREIGN_TEST_OK probes=13 native-import=deterministic rejection=3
