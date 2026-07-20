#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:?usage: materialize_fixtures.sh <output-directory>}"
mkdir -p "$OUT"

emit() {
  local name="$1" data="$2"
  printf '%s' "$data" | base64 -d > "$OUT/$name"
}

# The fixtures are split to keep each auditable source file bounded while preserving exact bytes.
source "$SCRIPT_DIR/fixtures/current.inc"
source "$SCRIPT_DIR/fixtures/signature-expiry.inc"
source "$SCRIPT_DIR/fixtures/consistency-rollback.inc"

# Public/runtime filenames are stable. Signed header revisions remain internal anti-rollback counters.
mv "$OUT/1.root.zrm" "$OUT/root-bootstrap.zrm"
mv "$OUT/2.root.zrm" "$OUT/root.zrm"
mv "$OUT/3.targets.zrm" "$OUT/targets.zrm"
mv "$OUT/7.native-apps.zrm" "$OUT/native-apps.zrm"
mv "$OUT/5.snapshot.zrm" "$OUT/snapshot.zrm"

echo "ZENREPO_FIXTURES_READY directory=$OUT count=14"
