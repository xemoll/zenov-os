#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build/zenuniverse-runtime-test}"
CXX="${CXX:-g++}"
rm -rf "$BUILD"
mkdir -p "$BUILD"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  "$ROOT/tools/zenuniverse/main.cpp" -o "$BUILD/zenuniverse"

"$BUILD/zenuniverse" host-profile --name zenov-0.1.1-i686 > "$BUILD/host.log"
grep -q '^architecture=x86$' "$BUILD/host.log"
grep -q '^capability=loader.elf32-static$' "$BUILD/host.log"
! grep -q '^capability=kernel.threads$' "$BUILD/host.log"
grep -q '^ZENUNIVERSE_HOST_PROFILE_OK capabilities=9$' "$BUILD/host.log"

set +e
"$BUILD/zenuniverse" runtime-plan --input "$ROOT/packages/universe" \
  --package org.zenov.profile.playstation1-game --host-profile zenov-0.1.1-i686 \
  > "$BUILD/ps1.log" 2>&1
ps1_status=$?
"$BUILD/zenuniverse" runtime-plan --input "$ROOT/packages/universe" \
  --package org.zenov.profile.psp-game --host-profile zenov-0.1.1-i686 \
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

grep -q 'provider architecture mismatch: provider=org.zenov.runtime.duckstation package=x86_64 host=x86' "$BUILD/ps1.log"
grep -q 'provider not available yet: org.zenov.runtime.duckstation (planned)' "$BUILD/ps1.log"
grep -q '^required-asset=firmware.playstation1-bios source=user-supplied$' "$BUILD/ps1.log"
grep -q 'missing capability provider: graphics.opengl3.1-or-vulkan1.0' "$BUILD/ps1.log"
grep -q 'missing capability provider: kernel.jit' "$BUILD/ps1.log"
grep -q 'ZENUNIVERSE_RUNTIME_BLOCKED package=org.zenov.profile.playstation1-game' "$BUILD/ps1.log"
[[ $(grep -c 'missing capability provider: storage.large-files' "$BUILD/ps1.log") -eq 1 ]]

grep -q 'provider architecture mismatch: provider=org.zenov.runtime.ppsspp package=x86_64 host=x86' "$BUILD/psp.log"
grep -q 'missing capability provider: graphics.opengl3.0-or-vulkan' "$BUILD/psp.log"
grep -q 'ZENUNIVERSE_RUNTIME_BLOCKED package=org.zenov.profile.psp-game' "$BUILD/psp.log"

grep -q '^accepts=bin-cue$' "$BUILD/duckstation.log"
grep -q '^accepts=chd$' "$BUILD/duckstation.log"
grep -q '^accepts=pbp$' "$BUILD/duckstation.log"
grep -q '^accepts=psx-exe$' "$BUILD/duckstation.log"

cp "$ROOT/packages/universe/profile-playstation1-game.zsource" "$BUILD/invalid-accepts.zsource"
printf '\naccepts=chd\n' >> "$BUILD/invalid-accepts.zsource"
if "$BUILD/zenuniverse" validate "$BUILD/invalid-accepts.zsource" > "$BUILD/invalid.log" 2>&1; then
  echo 'non-runtime descriptor unexpectedly accepted provider artifact declarations' >&2
  exit 1
fi
grep -q 'only runtime providers may declare accepts' "$BUILD/invalid.log"

"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  "$ROOT/tools/zenuniverse/main.cpp" -o "$BUILD/zenuniverse-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$BUILD/zenuniverse-sanitized" runtime-status --input "$ROOT/packages/universe" \
  --runtime duckstation --host-profile zenov-0.1.1-i686 \
  > "$BUILD/sanitized.log" 2>&1 || [[ $? -eq 3 ]]
grep -q 'ZENUNIVERSE_RUNTIME_BLOCKED' "$BUILD/sanitized.log"

printf 'ZENUNIVERSE_RUNTIME_PROVIDER_TESTS_OK host-profile=verified provider-architecture=checked ps1=blocked-honestly psp=blocked-honestly assets=user-supplied sanitizers=yes\n'
