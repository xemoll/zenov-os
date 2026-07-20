#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-g++}"
BUILD="${BUILD:-$ROOT/build/zenrepo-test}"
MATERIALIZER="$ROOT/tools/zenrepo/materialize_fixtures.sh"
rm -rf "$BUILD"
mkdir -p "$BUILD/current" "$BUILD/fixtures"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic "$ROOT/tools/zenrepo/main.cpp" -o "$BUILD/zenrepo"
bash "$MATERIALIZER" "$BUILD/fixtures" >/dev/null
for name in root-bootstrap.zrm root.zrm targets.zrm native-apps.zrm snapshot.zrm timestamp.zrm; do
  cp "$BUILD/fixtures/$name" "$BUILD/current/$name"
done
"$BUILD/zenrepo" verify --metadata "$BUILD/current" --time 1767225600 --state "$BUILD/state.bin" | grep -q 'ZENREPO_VERIFY_OK trust=verified packages=2'
"$BUILD/zenrepo" verify --metadata "$BUILD/current" --time 1767225600 --state "$BUILD/state.bin" >/dev/null

negative() {
  local name="$1"
  shift
  rm -rf "$BUILD/$name"
  cp -a "$BUILD/current" "$BUILD/$name"
  "$@"
  if "$BUILD/zenrepo" verify --metadata "$BUILD/$name" --time 1767225600 --state "$BUILD/${name}.state" >"$BUILD/${name}.out" 2>&1; then
    echo "negative fixture unexpectedly verified: $name" >&2
    exit 1
  fi
}
negative expired cp "$BUILD/fixtures/expired.timestamp.zrm" "$BUILD/expired/timestamp.zrm"
negative mixmatch bash -c "cp '$BUILD/fixtures/mixmatch.snapshot.zrm' '$BUILD/mixmatch/snapshot.zrm'; cp '$BUILD/fixtures/mixmatch.timestamp.zrm' '$BUILD/mixmatch/timestamp.zrm'"
negative badsig bash -c "cp '$BUILD/fixtures/bad-signature.native-apps.zrm' '$BUILD/badsig/native-apps.zrm'; cp '$BUILD/fixtures/bad-signature.snapshot.zrm' '$BUILD/badsig/snapshot.zrm'; cp '$BUILD/fixtures/bad-signature.timestamp.zrm' '$BUILD/badsig/timestamp.zrm'"

rm -rf "$BUILD/rollback-state"
cp -a "$BUILD/current" "$BUILD/rollback-state"
"$BUILD/zenrepo" verify --metadata "$BUILD/rollback-state" --time 1767225600 --state "$BUILD/rollback-state.bin" >/dev/null
cp "$BUILD/fixtures/rollback.snapshot.zrm" "$BUILD/rollback-state/snapshot.zrm"
cp "$BUILD/fixtures/rollback.timestamp.zrm" "$BUILD/rollback-state/timestamp.zrm"
if "$BUILD/zenrepo" verify --metadata "$BUILD/rollback-state" --time 1767225600 --state "$BUILD/rollback-state.bin" >"$BUILD/rollback-state.out" 2>&1; then
  echo "persisted rollback unexpectedly verified" >&2
  exit 1
fi

printf '\xff' | dd of="$BUILD/state.bin" bs=1 seek=0 conv=notrunc status=none
if "$BUILD/zenrepo" verify --metadata "$BUILD/current" --time 1767225600 --state "$BUILD/state.bin" >"$BUILD/state-corrupt.out" 2>&1; then
  echo "corrupt state unexpectedly accepted" >&2
  exit 1
fi

echo "ZENREPO_TESTS_OK"
