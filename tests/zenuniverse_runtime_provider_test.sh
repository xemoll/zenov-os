#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build/zenuniverse-runtime-test}"
CXX="${CXX:-g++}"
CLANGXX="${CLANGXX:-clang++}"
rm -rf "$BUILD"
mkdir -p "$BUILD/fixtures" "$BUILD/negative"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  "$ROOT/tools/zenuniverse/main.cpp" -o "$BUILD/zenuniverse"

"$BUILD/zenuniverse" host-profile --name zenov-0.1.1-i686 > "$BUILD/host.log"
grep -q '^architecture=x86$' "$BUILD/host.log"
grep -q '^artifact-bytes-limit=65536$' "$BUILD/host.log"
grep -q '^process-limit=1$' "$BUILD/host.log"
grep -q '^thread-limit=1$' "$BUILD/host.log"
grep -q '^capability=loader.elf32-static$' "$BUILD/host.log"
grep -q '^capability=abi.linux.i386.int80-minimal$' "$BUILD/host.log"
! grep -q '^capability=kernel.threads$' "$BUILD/host.log"
grep -q '^ZENUNIVERSE_HOST_PROFILE_OK capabilities=10$' "$BUILD/host.log"

"$BUILD/zenuniverse" runtime-status --input "$ROOT/packages/universe" \
  --runtime native --host-profile zenov-0.1.1-i686 > "$BUILD/native-status.log"
grep -q '^provider=org.zenov.runtime.native@1.0.0$' "$BUILD/native-status.log"
grep -q '^provider-abi=zen-runtime-provider-1$' "$BUILD/native-status.log"
grep -q '^launch-mode=builtin$' "$BUILD/native-status.log"
grep -q '^ZENUNIVERSE_RUNTIME_READY package=org.zenov.runtime.native$' "$BUILD/native-status.log"

"$BUILD/zenuniverse" runtime-status --input "$ROOT/packages/universe" \
  --runtime linux-i386-minimal --host-profile zenov-0.1.1-i686 > "$BUILD/linux-i386-status.log"
grep -q '^provider=org.zenov.runtime.linux-i386-minimal@1.0.0$' "$BUILD/linux-i386-status.log"
grep -q '^launch-mode=builtin$' "$BUILD/linux-i386-status.log"
grep -q '^ZENUNIVERSE_RUNTIME_READY package=org.zenov.runtime.linux-i386-minimal$' "$BUILD/linux-i386-status.log"

"$BUILD/zenuniverse" runtime-status --input "$ROOT/packages/universe" \
  --runtime psx-r3000a-diagnostic --host-profile zenov-0.1.1-i686 > "$BUILD/psx-diagnostic-status.log"
grep -q '^provider=org.zenov.runtime.psx-r3000a-diagnostic@0.1.1$' "$BUILD/psx-diagnostic-status.log"
grep -q '^launch-mode=builtin$' "$BUILD/psx-diagnostic-status.log"
grep -q '^ZENUNIVERSE_RUNTIME_READY package=org.zenov.runtime.psx-r3000a-diagnostic$' "$BUILD/psx-diagnostic-status.log"

"$BUILD/zenuniverse" runtime-plan --input "$ROOT/packages/universe" \
  --package org.zenov.profile.playstation1-diagnostic --host-profile zenov-0.1.1-i686 \
  --artifact psx-exe > "$BUILD/psx-diagnostic-plan.log"
grep -q 'install org.zenov.runtime.psx-r3000a-diagnostic@0.1.1' "$BUILD/psx-diagnostic-plan.log"
grep -q '^ZENUNIVERSE_RUNTIME_READY package=org.zenov.profile.playstation1-diagnostic$' "$BUILD/psx-diagnostic-plan.log"

set +e
"$BUILD/zenuniverse" runtime-plan --input "$ROOT/packages/universe" \
  --package org.zenov.profile.playstation1-game --host-profile zenov-0.1.1-i686 --artifact chd \
  > "$BUILD/ps1.log" 2>&1
ps1_status=$?
"$BUILD/zenuniverse" runtime-plan --input "$ROOT/packages/universe" \
  --package org.zenov.profile.psp-game --host-profile zenov-0.1.1-i686 --artifact cso \
  > "$BUILD/psp.log" 2>&1
psp_status=$?
"$BUILD/zenuniverse" runtime-status --input "$ROOT/packages/universe" \
  --runtime duckstation --host-profile zenov-0.1.1-i686 \
  > "$BUILD/duckstation.log" 2>&1
duckstation_status=$?
set -e
[[ $ps1_status -eq 3 ]]
[[ $psp_status -eq 3 ]]
[[ $duckstation_status -eq 3 ]]

grep -q 'install org.zenov.runtime.native@1.0.0' "$BUILD/ps1.log"
grep -q 'provider architecture mismatch: provider=org.zenov.runtime.duckstation package=x86_64 host=x86' "$BUILD/ps1.log"
grep -q 'provider not available yet: org.zenov.runtime.duckstation (planned)' "$BUILD/ps1.log"
grep -q '^required-asset=firmware.playstation1-bios source=user-supplied$' "$BUILD/ps1.log"
grep -q '^capability-alternative=graphics.opengl3.1|graphics.vulkan1.0 selected=- satisfied=no$' "$BUILD/ps1.log"
grep -q 'no satisfiable capability alternative: graphics.opengl3.1|graphics.vulkan1.0' "$BUILD/ps1.log"
! grep -q 'graphics.opengl3.1-or-vulkan1.0' "$BUILD/ps1.log"
grep -q '^ZENUNIVERSE_RUNTIME_BLOCKED package=org.zenov.profile.playstation1-game reasons=11$' "$BUILD/ps1.log"
[[ $(grep -c 'missing capability provider: storage.large-files' "$BUILD/ps1.log") -eq 1 ]]

grep -q 'provider architecture mismatch: provider=org.zenov.runtime.ppsspp package=x86_64 host=x86' "$BUILD/psp.log"
grep -q '^capability-alternative=graphics.opengl3.0|graphics.vulkan1.0 selected=- satisfied=no$' "$BUILD/psp.log"
grep -q 'ZENUNIVERSE_RUNTIME_BLOCKED package=org.zenov.profile.psp-game' "$BUILD/psp.log"

grep -q '^accepts=bin-cue$' "$BUILD/duckstation.log"
grep -q '^accepts=chd$' "$BUILD/duckstation.log"
grep -q '^accepts=pbp$' "$BUILD/duckstation.log"
grep -q '^accepts=psx-exe$' "$BUILD/duckstation.log"

printf 'ZEX1\001\000\000\000native-runtime-provider-fixture\n' > "$BUILD/fixtures/hello.zex"
"$BUILD/zenuniverse" artifact-manifest --input "$ROOT/packages/universe" \
  --profile org.zenov.profile.native-app --artifact zex1 --file "$BUILD/fixtures/hello.zex" \
  --ownership redistributable --output "$BUILD/fixtures/hello.zartifact" > "$BUILD/native-manifest.log"
grep -q '^ZENUNIVERSE_ARTIFACT_MANIFEST_OK profile=org.zenov.profile.native-app artifact=zex1 ' "$BUILD/native-manifest.log"
"$BUILD/zenuniverse" verify-artifact --manifest "$BUILD/fixtures/hello.zartifact" \
  --file "$BUILD/fixtures/hello.zex" > "$BUILD/native-verify.log"
grep -q '^ZENUNIVERSE_ARTIFACT_VERIFIED profile=org.zenov.profile.native-app artifact=zex1 ' "$BUILD/native-verify.log"
"$BUILD/zenuniverse" launch-plan --input "$ROOT/packages/universe" \
  --manifest "$BUILD/fixtures/hello.zartifact" --file "$BUILD/fixtures/hello.zex" \
  --host-profile zenov-0.1.1-i686 --output "$BUILD/fixtures/hello.zlaunch" > "$BUILD/native-launch.log"
grep -q '^provider=org.zenov.runtime.native@1.0.0$' "$BUILD/native-launch.log"
grep -q '^entrypoint=@kernel-loader$' "$BUILD/native-launch.log"
grep -q '^ZENUNIVERSE_LAUNCH_READY profile=org.zenov.profile.native-app reasons=0$' "$BUILD/native-launch.log"
cmp "$BUILD/native-launch.log" "$BUILD/fixtures/hello.zlaunch"

printf '\177ELF\001\001\001\000linux-i386-minimal-fixture\n' > "$BUILD/fixtures/linux-i386.elf"
"$BUILD/zenuniverse" artifact-manifest --input "$ROOT/packages/universe" \
  --profile org.zenov.profile.linux-i386-static --artifact elf32 --file "$BUILD/fixtures/linux-i386.elf" \
  --ownership redistributable --output "$BUILD/fixtures/linux-i386.zartifact" > "$BUILD/linux-i386-manifest.log"
"$BUILD/zenuniverse" launch-plan --input "$ROOT/packages/universe" \
  --manifest "$BUILD/fixtures/linux-i386.zartifact" --file "$BUILD/fixtures/linux-i386.elf" \
  --host-profile zenov-0.1.1-i686 --output "$BUILD/fixtures/linux-i386.zlaunch" > "$BUILD/linux-i386-launch.log"
grep -q '^provider=org.zenov.runtime.linux-i386-minimal@1.0.0$' "$BUILD/linux-i386-launch.log"
grep -q '^entrypoint=@kernel-loader$' "$BUILD/linux-i386-launch.log"
grep -q '^ZENUNIVERSE_LAUNCH_READY profile=org.zenov.profile.linux-i386-static reasons=0$' "$BUILD/linux-i386-launch.log"
cmp "$BUILD/linux-i386-launch.log" "$BUILD/fixtures/linux-i386.zlaunch"

python3 - "$BUILD" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1]) / 'fixtures'
root.joinpath('game.chd').write_bytes((b'CHDTEST' * 20000)[:100000])
root.joinpath('ps1-bios.bin').write_bytes(b'PS1BIOS-FIXTURE' * 64)
PY
"$BUILD/zenuniverse" artifact-manifest --input "$ROOT/packages/universe" \
  --profile org.zenov.profile.playstation1-game --artifact chd --file "$BUILD/fixtures/game.chd" \
  --ownership user-owned --output "$BUILD/fixtures/game.zartifact" > "$BUILD/ps1-manifest.log"

set +e
"$BUILD/zenuniverse" launch-plan --input "$ROOT/packages/universe" \
  --manifest "$BUILD/fixtures/game.zartifact" --file "$BUILD/fixtures/game.chd" \
  --host-profile zenov-0.1.1-i686 > "$BUILD/ps1-launch-missing.log" 2>&1
missing_status=$?
"$BUILD/zenuniverse" launch-plan --input "$ROOT/packages/universe" \
  --manifest "$BUILD/fixtures/game.zartifact" --file "$BUILD/fixtures/game.chd" \
  --host-profile zenov-0.1.1-i686 \
  --asset firmware.playstation1-bios="$BUILD/fixtures/ps1-bios.bin" > "$BUILD/ps1-launch-bios.log" 2>&1
bios_status=$?
set -e
[[ $missing_status -eq 3 ]]
[[ $bios_status -eq 3 ]]
grep -q 'blocked: artifact exceeds host profile storage limit: bytes=100000 limit=65536' "$BUILD/ps1-launch-missing.log"
grep -q 'blocked: required asset not supplied: firmware.playstation1-bios' "$BUILD/ps1-launch-missing.log"
grep -q '^ZENUNIVERSE_LAUNCH_BLOCKED profile=org.zenov.profile.playstation1-game reasons=13$' "$BUILD/ps1-launch-missing.log"
grep -q '^asset=firmware.playstation1-bios bytes=960 sha256=' "$BUILD/ps1-launch-bios.log"
! grep -q 'required asset not supplied' "$BUILD/ps1-launch-bios.log"
grep -q '^ZENUNIVERSE_LAUNCH_BLOCKED profile=org.zenov.profile.playstation1-game reasons=12$' "$BUILD/ps1-launch-bios.log"

cp "$BUILD/fixtures/hello.zex" "$BUILD/fixtures/hello-tampered.zex"
printf X >> "$BUILD/fixtures/hello-tampered.zex"
if "$BUILD/zenuniverse" verify-artifact --manifest "$BUILD/fixtures/hello.zartifact" \
  --file "$BUILD/fixtures/hello-tampered.zex" > "$BUILD/tamper.log" 2>&1; then
  echo 'tampered artifact unexpectedly verified' >&2
  exit 1
fi
grep -q 'artifact size mismatch' "$BUILD/tamper.log"

ln -s hello.zex "$BUILD/fixtures/hello-link.zex"
if "$BUILD/zenuniverse" artifact-manifest --input "$ROOT/packages/universe" \
  --profile org.zenov.profile.native-app --artifact zex1 --file "$BUILD/fixtures/hello-link.zex" \
  --ownership redistributable --output "$BUILD/fixtures/link.zartifact" > "$BUILD/symlink.log" 2>&1; then
  echo 'symlink artifact unexpectedly accepted' >&2
  exit 1
fi
grep -q 'symbolic links are not accepted' "$BUILD/symlink.log"

if "$BUILD/zenuniverse" artifact-manifest --input "$ROOT/packages/universe" \
  --profile org.zenov.profile.playstation1-game --artifact cso --file "$BUILD/fixtures/game.chd" \
  --ownership user-owned --output "$BUILD/fixtures/unsupported.zartifact" > "$BUILD/unsupported.log" 2>&1; then
  echo 'unsupported provider artifact unexpectedly accepted' >&2
  exit 1
fi
grep -q 'does not accept artifact cso' "$BUILD/unsupported.log"

if "$BUILD/zenuniverse" artifact-manifest --input "$ROOT/packages/universe" \
  --profile org.zenov.profile.playstation1-game --artifact chd --file "$BUILD/fixtures/game.chd" \
  --ownership redistributable --output "$BUILD/fixtures/ownership.zartifact" > "$BUILD/ownership.log" 2>&1; then
  echo 'console artifact without user-owned declaration unexpectedly accepted' >&2
  exit 1
fi
grep -q 'console artifacts must use ownership=user-owned' "$BUILD/ownership.log"

cp "$ROOT/packages/universe/runtime-duckstation.zsource" "$BUILD/negative/unknown-capability.zsource"
sed -i 's/requires=kernel.threads/requires=kernel.threadz/' "$BUILD/negative/unknown-capability.zsource"
if "$BUILD/zenuniverse" validate "$BUILD/negative/unknown-capability.zsource" > "$BUILD/unknown-capability.log" 2>&1; then
  echo 'unknown capability unexpectedly accepted' >&2
  exit 1
fi
grep -q 'unknown or duplicate requires' "$BUILD/unknown-capability.log"

SANITIZER_CXX="$CLANGXX"
SANITIZER_FLAGS=(-fsanitize=address,undefined,unsigned-integer-overflow,implicit-unsigned-integer-truncation,implicit-signed-integer-truncation,implicit-integer-sign-change)
if ! command -v "$SANITIZER_CXX" >/dev/null 2>&1; then
  SANITIZER_CXX="$CXX"
  SANITIZER_FLAGS=(-fsanitize=address,undefined)
fi
"$SANITIZER_CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
  "${SANITIZER_FLAGS[@]}" \
  -fno-sanitize-recover=all -fno-omit-frame-pointer \
  "$ROOT/tools/zenuniverse/main.cpp" -o "$BUILD/zenuniverse-sanitized"
ASAN_OPTIONS="detect_leaks=${ZENUNIVERSE_DETECT_LEAKS:-1}:halt_on_error=1" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$BUILD/zenuniverse-sanitized" launch-plan --input "$ROOT/packages/universe" \
  --manifest "$BUILD/fixtures/hello.zartifact" --file "$BUILD/fixtures/hello.zex" \
  --host-profile zenov-0.1.1-i686 > "$BUILD/sanitized.log"
grep -q 'ZENUNIVERSE_LAUNCH_READY' "$BUILD/sanitized.log"

printf 'ZENUNIVERSE_RUNTIME_PROVIDER_TESTS_OK schema=v1 host-profile=verified native=ready linux-i386=ready psx-diagnostic=ready artifact-manifest=content-addressed launch-plan=verified alternatives=typed ps1-games=blocked-honestly assets=hashed tamper=blocked symlink=blocked sanitizers=yes\n'
