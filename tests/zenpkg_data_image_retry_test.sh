#!/usr/bin/env bash
set -euo pipefail

MAKE_BIN="${1:-make}"
BUILD_DIR="${2:-build}"
IMAGE="$BUILD_DIR/zenov-data.img"
STAMP="$BUILD_DIR/zenpkg-data.stamp"
STAGE="$BUILD_DIR/.zenov-data.packaging.tmp"

[[ -f "$IMAGE" && -f "$STAMP" ]] || {
  echo 'zenpkg-data-retry: packaged image and stamp are required' >&2
  exit 2
}

before="$(sha256sum "$IMAGE" | cut -d' ' -f1)"
rm -f "$STAMP"
"$MAKE_BIN" "$STAMP"
after="$(sha256sum "$IMAGE" | cut -d' ' -f1)"

[[ "$before" == "$after" ]] || {
  echo "zenpkg-data-retry: image changed across stamp recovery: $before -> $after" >&2
  exit 1
}
[[ ! -e "$STAGE" ]] || {
  echo "zenpkg-data-retry: staging image leaked: $STAGE" >&2
  exit 1
}

printf 'ZENPKG_DATA_RETRY_TEST_OK image_sha256=%s atomic=1 idempotent=1 staging-clean=1\n' "$after"
