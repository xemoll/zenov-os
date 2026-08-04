#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build/zenuniverse-test}"
CXX="${CXX:-g++}"
rm -rf "$BUILD"
mkdir -p "$BUILD/catalog" "$BUILD/catalog-copy" "$BUILD/negative"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  "$ROOT/tools/zenuniverse/main.cpp" -o "$BUILD/zenuniverse"
"$BUILD/zenuniverse" self-test
"$BUILD/zenuniverse" validate "$ROOT"/packages/universe/*.zsource | grep -q 'ZENUNIVERSE_VALIDATE_OK count=26'
"$BUILD/zenuniverse" compile --input "$ROOT/packages/universe" --output "$BUILD/universe.zuc"
cp -a "$ROOT/packages/universe/." "$BUILD/catalog-copy/"
"$BUILD/zenuniverse" compile --input "$BUILD/catalog-copy" --output "$BUILD/universe-copy.zuc"
cmp "$BUILD/universe.zuc" "$BUILD/universe-copy.zuc"
grep -q '^ZENUNIVERSE1$' "$BUILD/universe.zuc"
grep -q '^count=26$' "$BUILD/universe.zuc"
grep -q '^provider-abi=zen-runtime-provider-1$' "$BUILD/universe.zuc"
grep -q '^requires-any=graphics.opengl3.1|graphics.vulkan1.0$' "$BUILD/universe.zuc"
! grep -q -- '-or-vulkan' "$BUILD/universe.zuc"

cat > "$BUILD/catalog/runtime-native.zsource" <<'EOF_NATIVE'
schema=zen-source-1
id=test.runtime.native
version=1.0.0
kind=runtime
platform=zenov
architecture=x86_64
artifact=runtime-bundle
delivery=builtin
runtime=native
availability=available
entrypoint=@kernel-loader
provider-abi=zen-runtime-provider-1
launch-mode=builtin
channel=test
category=system
license=Test-only
description=Test built-in native runtime provider.
homepage=https://example.test/runtime-native
bytes=0
sha256=-
accepts=elf64
accepts=zex1
provides=runtime.native
EOF_NATIVE

cat > "$BUILD/catalog/runtime-wine.zsource" <<'EOF_WINE'
schema=zen-source-1
id=test.runtime.wine
version=1.0.0
kind=runtime
platform=zenov
architecture=x86_64
artifact=runtime-bundle
delivery=embedded
runtime=native
availability=available
entrypoint=/system/runtimes/wine/bin/wine
provider-abi=zen-runtime-provider-1
launch-mode=exec
channel=test
category=compatibility
license=Test-only
description=Test runtime provider.
homepage=https://example.test/runtime-wine
bytes=7
sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
accepts=exe
accepts=msi
requires=kernel.processes
requires=kernel.threads
provides=runtime.wine
arg=%artifact%
EOF_WINE

cat > "$BUILD/catalog/runtime-proton.zsource" <<'EOF_PROTON'
schema=zen-source-1
id=test.runtime.proton
version=1.0.0
kind=runtime
platform=zenov
architecture=x86_64
artifact=runtime-bundle
delivery=embedded
runtime=native
availability=available
entrypoint=/system/runtimes/proton/proton
provider-abi=zen-runtime-provider-1
launch-mode=exec
channel=test
category=gaming
license=Test-only
description=Test game runtime provider.
homepage=https://example.test/runtime-proton
bytes=7
sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
accepts=exe
requires=runtime.wine
requires=audio.low-latency
requires=input.gamepad
requires-any=graphics.opengl3.3|graphics.vulkan1.0
provides=runtime.proton
arg=%artifact%
EOF_PROTON

cat > "$BUILD/catalog/windows-game.zsource" <<'EOF_WINDOWS'
schema=zen-source-1
id=test.windows.game
version=2.0.0
kind=game
platform=windows
architecture=x86_64
artifact=exe
delivery=https
runtime=proton
availability=available
entrypoint=game.exe
channel=test
category=gaming
license=Test-only
description=Test downloadable Windows game artifact.
homepage=https://example.test/windows-game
bytes=123456
sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
mirror=https://downloads.example.test/windows-game-2.0.0.exe
requires=storage.large-files
EOF_WINDOWS

cat > "$BUILD/catalog/console-game.zsource" <<'EOF_CONSOLE'
schema=zen-source-1
id=test.playstation2.game
version=1.0.0
kind=game
platform=playstation2
architecture=x86_64
artifact=disc-image
delivery=user-supplied
runtime=pcsx2
availability=external
entrypoint=%user-asset%
channel=test
category=gaming
license=User-owned-content
description=Test legal import profile.
homepage=https://example.test/ps2-game
bytes=0
sha256=-
requires=storage.large-files
EOF_CONSOLE

cat > "$BUILD/catalog/runtime-pcsx2.zsource" <<'EOF_PCSX2'
schema=zen-source-1
id=test.runtime.pcsx2
version=1.0.0
kind=runtime
platform=zenov
architecture=x86_64
artifact=runtime-bundle
delivery=embedded
runtime=native
availability=available
entrypoint=/system/runtimes/pcsx2/pcsx2
provider-abi=zen-runtime-provider-1
launch-mode=exec
channel=test
category=gaming
license=Test-only
description=Test console runtime provider.
homepage=https://example.test/runtime-pcsx2
bytes=7
sha256=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
accepts=disc-image
accepts=iso
requires=audio.low-latency
requires=input.gamepad
requires-any=graphics.opengl3.3|graphics.vulkan1.1
provides=runtime.pcsx2
arg=%artifact%
EOF_PCSX2

set +e
"$BUILD/zenuniverse" resolve --input "$BUILD/catalog" --package test.windows.game --host-arch x86_64 \
  --capability kernel.processes --capability kernel.threads --capability storage.large-files \
  > "$BUILD/blocked.log" 2>&1
status=$?
set -e
[[ $status -eq 3 ]]
grep -q 'no satisfiable capability alternative: graphics.opengl3.3|graphics.vulkan1.0' "$BUILD/blocked.log"
grep -q 'ZENUNIVERSE_RESOLVE_BLOCKED' "$BUILD/blocked.log"

"$BUILD/zenuniverse" resolve --input "$BUILD/catalog" --package test.windows.game --host-arch x86_64 \
  --capability kernel.processes --capability kernel.threads --capability graphics.vulkan1.0 \
  --capability audio.low-latency --capability input.gamepad --capability storage.large-files \
  > "$BUILD/resolved.log"
grep -q 'install test.runtime.native@1.0.0' "$BUILD/resolved.log"
grep -q 'install test.runtime.wine@1.0.0' "$BUILD/resolved.log"
grep -q 'install test.runtime.proton@1.0.0' "$BUILD/resolved.log"
grep -q 'install test.windows.game@2.0.0' "$BUILD/resolved.log"
grep -q '^capability-alternative=graphics.opengl3.3|graphics.vulkan1.0 selected=graphics.vulkan1.0 satisfied=yes$' "$BUILD/resolved.log"
grep -q 'capability-source=manual-unverified' "$BUILD/resolved.log"
grep -q 'ZENUNIVERSE_RESOLVE_OK' "$BUILD/resolved.log"

"$BUILD/zenuniverse" fetch-plan --input "$BUILD/catalog" --package test.windows.game > "$BUILD/fetch.log"
grep -q '^mirror=https://downloads.example.test/windows-game-2.0.0.exe$' "$BUILD/fetch.log"
grep -q 'ZENUNIVERSE_FETCH_READY' "$BUILD/fetch.log"

"$BUILD/zenuniverse" resolve --input "$BUILD/catalog" --package test.playstation2.game --host-arch x86_64 \
  --capability graphics.vulkan1.1 --capability audio.low-latency --capability input.gamepad \
  --capability storage.large-files > "$BUILD/console.log"
grep -q 'asset: user-supplied' "$BUILD/console.log"
grep -q 'ZENUNIVERSE_RESOLVE_OK' "$BUILD/console.log"

cp "$BUILD/catalog/windows-game.zsource" "$BUILD/negative/http.zsource"
sed -i 's#https://downloads.example.test#http://downloads.example.test#' "$BUILD/negative/http.zsource"
if "$BUILD/zenuniverse" validate "$BUILD/negative/http.zsource"; then
  echo 'HTTP mirror unexpectedly accepted' >&2
  exit 1
fi

cp "$BUILD/catalog/console-game.zsource" "$BUILD/negative/console-download.zsource"
sed -i 's/delivery=user-supplied/delivery=https/' "$BUILD/negative/console-download.zsource"
sed -i 's/bytes=0/bytes=10/' "$BUILD/negative/console-download.zsource"
sed -i 's/sha256=-/sha256=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee/' "$BUILD/negative/console-download.zsource"
echo 'mirror=https://downloads.example.test/proprietary.iso' >> "$BUILD/negative/console-download.zsource"
if "$BUILD/zenuniverse" validate "$BUILD/negative/console-download.zsource"; then
  echo 'redistributable console game unexpectedly accepted' >&2
  exit 1
fi

cp "$BUILD/catalog/windows-game.zsource" "$BUILD/negative/runtime-mismatch.zsource"
sed -i 's/runtime=proton/runtime=darling/' "$BUILD/negative/runtime-mismatch.zsource"
if "$BUILD/zenuniverse" validate "$BUILD/negative/runtime-mismatch.zsource"; then
  echo 'runtime/platform mismatch unexpectedly accepted' >&2
  exit 1
fi

cp "$BUILD/catalog/runtime-proton.zsource" "$BUILD/negative/unknown-capability.zsource"
sed -i 's/requires=audio.low-latency/requires=audio.low-latenc/' "$BUILD/negative/unknown-capability.zsource"
if "$BUILD/zenuniverse" validate "$BUILD/negative/unknown-capability.zsource"; then
  echo 'unknown capability unexpectedly accepted' >&2
  exit 1
fi

if "$BUILD/zenuniverse" resolve --input "$BUILD/catalog" --package test.windows.game --host-arch x86_64 \
  --capability graphics.vulkann > "$BUILD/negative/manual-capability.log" 2>&1; then
  echo 'unknown manual capability unexpectedly accepted' >&2
  exit 1
fi
grep -q 'unknown manual capability' "$BUILD/negative/manual-capability.log"

"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$ROOT/tools/zenuniverse/main.cpp" -o "$BUILD/zenuniverse-sanitized"
ASAN_OPTIONS="detect_leaks=${ZENUNIVERSE_DETECT_LEAKS:-1}" "$BUILD/zenuniverse-sanitized" self-test
ASAN_OPTIONS="detect_leaks=${ZENUNIVERSE_DETECT_LEAKS:-1}" "$BUILD/zenuniverse-sanitized" compile \
  --input "$ROOT/packages/universe" --output "$BUILD/universe-sanitized.zuc"
cmp "$BUILD/universe.zuc" "$BUILD/universe-sanitized.zuc"

printf 'ZENUNIVERSE_TESTS_OK descriptors=26 deterministic=yes resolver=yes alternatives=typed native-provider=ready linux-i386=ready psx-diagnostic=ready legal-asset-policy=yes capability-registry=yes sanitizers=yes\n'
