#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -Eeuo pipefail

on_error() {
  local status=$?
  local source="${BASH_SOURCE[1]:-${BASH_SOURCE[0]}}"
  local line="${BASH_LINENO[0]:-0}"
  printf 'zenpkg-foreign-test: %s:%s: status=%s command=%q\n' \
    "$source" "$line" "$status" "$BASH_COMMAND" >&2
  exit "$status"
}
trap on_error ERR

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


def elf_ident(elf_class: int, endian: int, osabi: int, etype: int,
              machine: int, flags: int = 0) -> bytearray:
    data = bytearray(40)
    data[0:7] = b"\x7fELF" + bytes([elf_class, endian, 1])
    data[7] = osabi
    order = "<" if endian == 1 else ">"
    struct.pack_into(order + "HH", data, 16, etype, machine)
    struct.pack_into(order + "I", data, 36, flags)
    return data


def static_elf(flags: int = 5) -> bytearray:
    data = bytearray(4097)
    data[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", data, 16,
                     2, 3, 1, 0x1000, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    struct.pack_into("<IIIIIIII", data, 52,
                     1, 0x1000, 0x1000, 0x1000, 1, 1, flags, 0x1000)
    data[0x1000] = 0xC3
    return data


pe = bytearray(68)
pe[0:2] = b"MZ"
struct.pack_into("<I", pe, 0x3C, 0x40)
pe[0x40:0x44] = b"PE\0\0"
(out / "sample.exe").write_bytes(pe)
(out / "bare-mz.exe").write_bytes(b"MZ" + bytes(62))
(out / "sample.msi").write_bytes(bytes.fromhex("d0cf11e0a1b11ae1") + bytes(56))
(out / "sample.msix").write_bytes(b"PK\x03\x04" + bytes(60))
(out / "sample.cab").write_bytes(b"MSCF" + bytes(60))
(out / "sample.msu").write_bytes(b"MSCF" + bytes(60))
(out / "sample.wim").write_bytes(b"MSWIM\0\0\0" + bytes(56))
(out / "sample.deb").write_bytes(b"!<arch>\n" + bytes(56))
(out / "sample.rpm").write_bytes(bytes.fromhex("edabeedb") + bytes(60))
(out / "sample-v2.apk").write_bytes(b"\x1f\x8b\x08\0" + bytes(60))
(out / "sample-v3.apk").write_bytes(b"ADB.pckg" + bytes(56))
(out / "sample.pkg.tar.zst").write_bytes(bytes.fromhex("28b52ffd") + bytes(60))
(out / "sample.pkg").write_bytes(b"xar!" + bytes(60))
(out / "sample.AppImage").write_bytes(b"\x7fELF" + bytes(60))
(out / "sample.xbe").write_bytes(b"XBEH" + bytes(60))
(out / "sample.xex").write_bytes(b"XEX2" + bytes(60))
(out / "sample-stfs.bin").write_bytes(b"LIVE" + bytes(60))
(out / "sample.xvc").write_bytes(b"XVC fixture")
(out / "sample.msixvc2").write_bytes(b"MSIXVC2 fixture")
(out / "sample-psx.exe").write_bytes(b"PS-X EXE" + bytes(56))
(out / "EBOOT.PBP").write_bytes(b"\0PBP" + bytes(60))

ps3_pkg = bytearray(b"\x7fPKG\x80\0\0\x01")
psp_pkg = bytearray(b"\x7fPKG\x80\0\0\x02")
(out / "sample-ps3.pkg").write_bytes(ps3_pkg + bytes(56))
(out / "sample-psp.pkg").write_bytes(psp_pkg + bytes(56))
(out / "sample-ps4.pkg").write_bytes(b"\x7fCNT" + bytes(60))
(out / "PS3UPDAT.PUP").write_bytes(b"SCEUF" + bytes(59))
(out / "sample-ps3.self").write_bytes(b"SCE\0" + bytes(60))
(out / "sample-ps4.self").write_bytes(bytes.fromhex("4f153d1d") + bytes(60))
(out / "sample.vpk").write_bytes(b"PK\x03\x04" + bytes(60))
(out / "java-class.class").write_bytes(bytes.fromhex("cafebabe0000003d"))
(out / "unknown.dat").write_bytes(b"unknown fixture")
(out / "static.elf").write_bytes(static_elf())
(out / "wx.elf").write_bytes(static_elf(7))
(out / "foreign-x64.elf").write_bytes(elf_ident(2, 1, 0, 2, 62))
(out / "generic-mips.elf").write_bytes(elf_ident(1, 1, 0, 2, 8))
(out / "ps2.elf").write_bytes(elf_ident(1, 1, 0, 2, 8, 0x20924001))
(out / "ps4.elf").write_bytes(elf_ident(2, 1, 9, 0xFE00, 62))

# ELF32/i386 ET_EXEC with PT_INTERP. Import must reject it before output.
dynamic = bytearray(84)
dynamic[0:7] = b"\x7fELF\x01\x01\x01"
struct.pack_into("<HHIIIIIHHHHHH", dynamic, 16,
                 2, 3, 1, 0x1000, 52, 0, 0, 52, 32, 1, 0, 0, 0)
struct.pack_into("<IIIIIIII", dynamic, 52, 3, 0, 0, 0, 0, 0, 4, 1)
(out / "dynamic.elf").write_bytes(dynamic)

iso = bytearray(0x8006)
iso[0x8001:0x8006] = b"CD001"
(out / "game.iso").write_bytes(iso)
(out / "game.chd").write_bytes(b"MComprHD" + bytes(56))

large_dmg = bytearray(1024 * 1024)
large_dmg[-512:-508] = b"koly"
(out / "large.dmg").write_bytes(large_dmg)
PY

probe_expect() {
  local file="$1"
  local format="$2"
  local support="$3"
  local confidence="$4"
  local log="$OUT/$(basename "$file").probe.log"
  "$ZENPKG" probe "$file" > "$log"
  if ! grep -Fqx "format: $format" "$log" ||
     ! grep -Fqx "support: $support" "$log" ||
     ! grep -Fqx "confidence: $confidence" "$log" ||
     ! grep -Fq "PROBE_OK format=$format support=$support confidence=$confidence" "$log"; then
    printf 'probe mismatch: file=%s expected=%s/%s/%s\n' \
      "$file" "$format" "$support" "$confidence" >&2
    cat "$log" >&2
    return 1
  fi
}

probe_expect "$NATIVE_ZEX" zex1 host-import signature
probe_expect "$OUT/fixtures/static.elf" elf host-import signature
probe_expect "$OUT/fixtures/foreign-x64.elf" elf-foreign runtime-required signature
probe_expect "$OUT/fixtures/generic-mips.elf" elf-foreign runtime-required signature
probe_expect "$OUT/fixtures/ps2.elf" playstation-ps2-elf runtime-required signature
probe_expect "$OUT/fixtures/ps4.elf" playstation-self runtime-required signature
probe_expect "$OUT/fixtures/sample.exe" pe runtime-required signature
probe_expect "$OUT/fixtures/bare-mz.exe" unknown unsupported signature
probe_expect "$OUT/fixtures/sample.msi" msi runtime-required signature
probe_expect "$OUT/fixtures/sample.msix" msix runtime-required signature
probe_expect "$OUT/fixtures/sample.cab" cab inspect-only signature
probe_expect "$OUT/fixtures/sample.msu" msu inspect-only signature
probe_expect "$OUT/fixtures/sample.wim" wim inspect-only signature
probe_expect "$OUT/fixtures/sample.deb" deb inspect-only signature
probe_expect "$OUT/fixtures/sample.rpm" rpm inspect-only signature
probe_expect "$OUT/fixtures/sample-v2.apk" alpine-apk inspect-only signature
probe_expect "$OUT/fixtures/sample-v3.apk" alpine-apk inspect-only signature
probe_expect "$OUT/fixtures/sample.pkg.tar.zst" arch-package inspect-only signature
probe_expect "$OUT/fixtures/sample.AppImage" appimage runtime-required signature
probe_expect "$OUT/fixtures/sample.pkg" apple-pkg runtime-required signature
probe_expect "$OUT/fixtures/large.dmg" apple-dmg runtime-required signature
probe_expect "$OUT/fixtures/sample.xbe" xbox-xbe runtime-required signature
probe_expect "$OUT/fixtures/sample.xex" xbox-xex runtime-required signature
probe_expect "$OUT/fixtures/sample-stfs.bin" xbox-stfs runtime-required signature
probe_expect "$OUT/fixtures/sample.xvc" xbox-xvc partner-only extension-only
probe_expect "$OUT/fixtures/sample.msixvc2" xbox-msixvc2 partner-only extension-only
probe_expect "$OUT/fixtures/sample-psx.exe" playstation-psx-exe runtime-required signature
probe_expect "$OUT/fixtures/EBOOT.PBP" playstation-pbp runtime-required signature
probe_expect "$OUT/fixtures/sample-ps3.pkg" playstation-pkg partner-only signature
probe_expect "$OUT/fixtures/sample-psp.pkg" playstation-pkg partner-only signature
probe_expect "$OUT/fixtures/sample-ps4.pkg" playstation-pkg partner-only signature
probe_expect "$OUT/fixtures/PS3UPDAT.PUP" playstation-pup partner-only signature
probe_expect "$OUT/fixtures/sample-ps3.self" playstation-self partner-only signature
probe_expect "$OUT/fixtures/sample-ps4.self" playstation-self partner-only signature
probe_expect "$OUT/fixtures/sample.vpk" playstation-pkg runtime-required signature
probe_expect "$OUT/fixtures/game.iso" disc-image runtime-required signature
probe_expect "$OUT/fixtures/game.chd" chd runtime-required signature
probe_expect "$OUT/fixtures/java-class.class" unknown unsupported signature
probe_expect "$OUT/fixtures/unknown.dat" unknown unsupported signature

grep -Fqx 'bytes: 1048576' "$OUT/large.dmg.probe.log"
grep -Fqx 'sampled_bytes: 66048' "$OUT/large.dmg.probe.log"
expected_large_sha="$(sha256sum "$OUT/fixtures/large.dmg" | awk '{print $1}')"
actual_large_sha="$(awk '/^sha256: / {print $2}' "$OUT/large.dmg.probe.log")"
test "$actual_large_sha" = "$expected_large_sha"

echo ZENPKG_FOREIGN_PROBE_OK cases=39 generations=legacy-current streaming=1

import_native_twice() {
  local input="$1"
  local name="$2"
  local payload="$3"
  local extension="$4"
  local first="$OUT/$name-a.zpk"
  local second="$OUT/$name-b.zpk"
  "$ZENPKG" import-native "$input" \
    --name "$name" --version 0.1.0 --license BSD-2-Clause \
    --source local-fixture --asset-policy redistributable \
    --output "$first" > "$OUT/$name-a.log"
  "$ZENPKG" import-native "$input" \
    --name "$name" --version 0.1.0 --license BSD-2-Clause \
    --source local-fixture --asset-policy redistributable \
    --output "$second" > "$OUT/$name-b.log"
  cmp "$first" "$second"
  "$ZENPKG" verify "$first" | grep -Fq "VERIFY_OK $name@0.1.0"
  "$ZENPKG" inspect "$first" > "$OUT/$name.inspect.log"
  grep -Fqx "payload_type: $payload" "$OUT/$name.inspect.log"
  grep -Fqx "entrypoint: /data/apps/pkg-$name-0.1.0.$extension" "$OUT/$name.inspect.log"
  probe_expect "$first" zenpkg installable signature
}

import_native_twice "$NATIVE_ZEX" imported-zex zex1 zex
import_native_twice "$OUT/fixtures/static.elf" imported-elf elf32 elf

echo ZENPKG_NATIVE_IMPORT_OK payloads=zex1,elf32 deterministic=2

reject_import() {
  local input="$1"
  local name="$2"
  local expected="$3"
  local output="$OUT/$name.zpk"
  local log="$OUT/$name.log"
  set +e
  "$ZENPKG" import-native "$input" \
    --name "$name" --version 0.1.0 --license BSD-2-Clause \
    --source local-fixture --asset-policy redistributable \
    --output "$output" > "$log" 2>&1
  local status=$?
  set -e
  test "$status" -eq 2
  grep -Fq "$expected" "$log"
  test ! -e "$output"
}

reject_import "$OUT/fixtures/sample.exe" rejected-pe \
  'format pe is not eligible for native import; support=runtime-required'
reject_import "$OUT/fixtures/dynamic.elf" rejected-dynamic \
  'dynamic ELF and PT_INTERP executables cannot be imported'
reject_import "$OUT/fixtures/wx.elf" rejected-wx \
  'ELF load segment violates the ZenovOS loader contract'
reject_import "$OUT/fixtures/foreign-x64.elf" rejected-x64 \
  'format elf-foreign is not eligible for native import; support=runtime-required'
reject_import "$OUT/fixtures/generic-mips.elf" rejected-mips \
  'format elf-foreign is not eligible for native import; support=runtime-required'
reject_import "$OUT/fixtures/ps2.elf" rejected-ps2 \
  'format playstation-ps2-elf is not eligible for native import; support=runtime-required'

cp "$NATIVE_ZEX" "$OUT/corrupt.zex"
python3 - "$OUT/corrupt.zex" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
if not data:
    raise SystemExit("cannot corrupt an empty ZEX1 fixture")
data[-1] ^= 0xFF
path.write_bytes(data)
PY
reject_import "$OUT/corrupt.zex" corrupt-zex 'ZEX1 image checksum mismatch'

echo ZENPKG_FOREIGN_REJECTION_OK cases=7
echo ZENPKG_FOREIGN_TEST_OK probes=39 native-import=zex1,elf32 deterministic=2 rejection=7 generations=legacy-current streaming=1
