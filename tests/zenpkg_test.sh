#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
read -r -a CXX_COMMAND <<< "${CXX:-g++}"
read -r -a EXTRA_CXXFLAGS <<< "${CXXFLAGS:-}"
BUILD="${ZENPKG_TEST_BUILD:-$ROOT/build/zenpkg-test}"
BIN="$BUILD/zenpkg"
TMP="$BUILD/tmp"

rm -rf "$BUILD"
mkdir -p "$TMP/repository"

"${CXX_COMMAND[@]}" -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  "${EXTRA_CXXFLAGS[@]}" "$ROOT/tools/zenpkg/main.cpp" -o "$BIN"

printf 'abc' > "$TMP/abc"
test "$("$BIN" hash "$TMP/abc")" = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
echo SHA256_KNOWN_VECTOR_OK

printf '\x7fELFZENOVOS-TEST-PAYLOAD\n' > "$TMP/hello.elf"
"$BIN" manifest-check "$ROOT/packages/examples/hello-native.zpkgmanifest" \
  --payload-size "$(stat -c%s "$TMP/hello.elf")" > "$TMP/manifest-check.log"
grep -q '^MANIFEST_OK hello-native@0.1.0$' "$TMP/manifest-check.log"
echo MANIFEST_VALIDATION_OK

"$BIN" pack --manifest "$ROOT/packages/examples/hello-native.zpkgmanifest" \
  --payload "$TMP/hello.elf" --output "$TMP/repository/hello-a.zpk" > "$TMP/pack-a.log"
"$BIN" pack --manifest "$ROOT/packages/examples/hello-native.zpkgmanifest" \
  --payload "$TMP/hello.elf" --output "$TMP/repository/hello-b.zpk" > "$TMP/pack-b.log"
cmp "$TMP/repository/hello-a.zpk" "$TMP/repository/hello-b.zpk"
echo PACKAGE_DETERMINISM_OK

"$BIN" verify "$TMP/repository/hello-a.zpk" | grep -q '^VERIFY_OK hello-native@0.1.0 '
"$BIN" inspect "$TMP/repository/hello-a.zpk" > "$TMP/inspect.log"
grep -q '^runtime: native$' "$TMP/inspect.log"
grep -q '^target: i686-zenov-none$' "$TMP/inspect.log"
echo PACKAGE_VERIFY_INSPECT_OK

"$BIN" extract "$TMP/repository/hello-a.zpk" \
  --manifest-output "$TMP/extracted.manifest" --payload-output "$TMP/extracted.payload" >/dev/null
cmp "$TMP/hello.elf" "$TMP/extracted.payload"
"$BIN" manifest-check "$TMP/extracted.manifest" --payload-size "$(stat -c%s "$TMP/extracted.payload")" >/dev/null
echo PACKAGE_EXTRACT_OK

"$BIN" resolve "$TMP/repository/hello-a.zpk" --architecture i686 --target i686-zenov-none --os-version 0.1.1 \
  --capability abi.elf32.i386.static --capability kernel.ring3 \
  --capability storage.zenovfs1 | grep -q '^RESOLVE_OK '
set +e
"$BIN" resolve "$TMP/repository/hello-a.zpk" --architecture i686 --target i686-zenov-none --os-version 0.1.1 \
  --capability kernel.ring3 > "$TMP/resolve-missing.log" 2>&1
status=$?
set -e
test "$status" -eq 3
grep -q 'missing capability: abi.elf32.i386.static' "$TMP/resolve-missing.log"
grep -q 'missing capability: storage.zenovfs1' "$TMP/resolve-missing.log"
set +e
"$BIN" resolve "$TMP/repository/hello-a.zpk" --architecture i686 --target x86_64-zenov-linux --os-version 0.1.1 \
  --capability abi.elf32.i386.static --capability kernel.ring3 \
  --capability storage.zenovfs1 > "$TMP/resolve-target.log" 2>&1
status=$?
set -e
test "$status" -eq 3
grep -q 'target mismatch' "$TMP/resolve-target.log"

set +e
"$BIN" resolve "$TMP/repository/hello-a.zpk" --architecture x86_64 --target i686-zenov-none --os-version 0.1.1 \
  --capability abi.elf32.i386.static --capability kernel.ring3 \
  --capability storage.zenovfs1 > "$TMP/resolve-architecture.log" 2>&1
status=$?
set -e
test "$status" -eq 3
grep -q 'architecture mismatch' "$TMP/resolve-architecture.log"
set +e
"$BIN" resolve "$TMP/repository/hello-a.zpk" --architecture i686 --target i686-zenov-none --os-version 0.1.0 \
  --capability abi.elf32.i386.static --capability kernel.ring3 \
  --capability storage.zenovfs1 > "$TMP/resolve-os-version.log" 2>&1
status=$?
set -e
test "$status" -eq 3
grep -q 'OS version too old' "$TMP/resolve-os-version.log"
echo CAPABILITY_RESOLUTION_OK

cp "$TMP/repository/hello-a.zpk" "$TMP/tampered.zpk"
printf '\x00' | dd of="$TMP/tampered.zpk" bs=1 seek=$(( $(stat -c%s "$TMP/tampered.zpk") - 1 )) conv=notrunc status=none
set +e
"$BIN" verify "$TMP/tampered.zpk" > "$TMP/tampered.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'payload SHA-256 mismatch' "$TMP/tampered.log"
echo PACKAGE_TAMPER_REJECTED

cp "$TMP/repository/hello-a.zpk" "$TMP/header-corrupt.zpk"
printf '\x01' | dd of="$TMP/header-corrupt.zpk" bs=1 seek=100 conv=notrunc status=none
set +e
"$BIN" verify "$TMP/header-corrupt.zpk" > "$TMP/header-corrupt.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'package header checksum mismatch' "$TMP/header-corrupt.log"
head -c -1 "$TMP/repository/hello-a.zpk" > "$TMP/truncated.zpk"
set +e
"$BIN" verify "$TMP/truncated.zpk" > "$TMP/truncated.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'package length does not match header sizes' "$TMP/truncated.log"
cp "$TMP/repository/hello-a.zpk" "$TMP/trailing.zpk"
printf 'X' >> "$TMP/trailing.zpk"
set +e
"$BIN" verify "$TMP/trailing.zpk" > "$TMP/trailing.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'package length does not match header sizes' "$TMP/trailing.log"
echo PACKAGE_STRUCTURE_CORRUPTION_REJECTED

cat > "$TMP/duplicate.manifest" <<'MANIFEST'
format=zenpkg-manifest-1
name=duplicate
name=duplicate-again
version=1.0.0
architecture=i686
target=i686-zenov-none
kind=application
entrypoint=/data/apps/DUP.ELF
payload_type=elf32
runtime=native
min_os=0.1.1
license=BSD-2-Clause
source=https://example.invalid/source
asset_policy=redistributable
MANIFEST
set +e
"$BIN" manifest-check "$TMP/duplicate.manifest" --payload-size 1 > "$TMP/duplicate.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'duplicate manifest key: name' "$TMP/duplicate.log"
echo DUPLICATE_FIELD_REJECTED

cat > "$TMP/traversal.manifest" <<'MANIFEST'
format=zenpkg-manifest-1
name=traversal
version=1.0.0
architecture=i686
target=i686-zenov-none
kind=application
entrypoint=/data/apps/..
payload_type=elf32
runtime=native
min_os=0.1.1
license=BSD-2-Clause
source=https://example.invalid/source
asset_policy=redistributable
MANIFEST
set +e
"$BIN" manifest-check "$TMP/traversal.manifest" --payload-size 1 > "$TMP/traversal.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'entrypoint must be' "$TMP/traversal.log"
echo PATH_TRAVERSAL_REJECTED

for manifest in "$ROOT"/packages/compat-profiles/*.zpkgmanifest; do
  output="$TMP/repository/$(basename "${manifest%.zpkgmanifest}").zpk"
  "$BIN" pack --manifest "$manifest" --payload - --output "$output" >/dev/null
  "$BIN" verify "$output" >/dev/null
done

set +e
"$BIN" index --input "$TMP/repository" --output "$TMP/duplicate-index.txt" > "$TMP/duplicate-index.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'duplicate package identity in index' "$TMP/duplicate-index.log"
echo DUPLICATE_PACKAGE_IDENTITY_REJECTED
rm "$TMP/repository/hello-b.zpk"
"$BIN" index --input "$TMP/repository" --output "$TMP/index-a.txt" >/dev/null
"$BIN" index --input "$TMP/repository" --output "$TMP/index-b.txt" >/dev/null
cmp "$TMP/index-a.txt" "$TMP/index-b.txt"
grep -q $'^package=compat-windows-proton\t1.0.0\tx86_64\tx86_64-zenov-linux\tcompat-profile\tproton\t' "$TMP/index-a.txt"
grep -q $'^package=hello-native\t0.1.0\ti686\ti686-zenov-none\tapplication\tnative\t' "$TMP/index-a.txt"
test "$(grep -c '^package=' "$TMP/index-a.txt")" -eq 7
echo REPOSITORY_INDEX_DETERMINISM_OK

mkdir -p "$TMP/unsafe-repository"
cp "$TMP/repository/hello-a.zpk" "$TMP/unsafe-repository/bad name.zpk"
set +e
"$BIN" index --input "$TMP/unsafe-repository" --output "$TMP/unsafe-index.txt" > "$TMP/unsafe-index.log" 2>&1
status=$?
set -e
test "$status" -eq 2
grep -q 'unsafe package filename' "$TMP/unsafe-index.log"
echo UNSAFE_INDEX_FILENAME_REJECTED

echo ZENPKG_TESTS_OK
