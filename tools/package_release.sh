#!/usr/bin/env bash
set -euo pipefail

BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
DIST="${3:-dist}"
PACKAGE="${4:-package}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.1.1"
REVISION="2"
RELEASE_ID="$VERSION-r$REVISION"

for tool in zip unzip sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "package-release: required tool not found: $tool" >&2
    exit 1
  }
done

[[ -f "$BOOT_IMAGE" ]] || { echo "package-release: boot image not found: $BOOT_IMAGE" >&2; exit 1; }
[[ -f "$DATA_IMAGE" ]] || { echo "package-release: data image not found: $DATA_IMAGE" >&2; exit 1; }
[[ "$(stat -c%s "$BOOT_IMAGE")" -eq 1474560 ]] || { echo "package-release: expected a 1,474,560-byte boot image" >&2; exit 1; }
[[ "$(stat -c%s "$DATA_IMAGE")" -eq 16777216 ]] || { echo "package-release: expected a 16,777,216-byte data image" >&2; exit 1; }

rm -rf "$DIST" "$PACKAGE"
mkdir -p "$DIST" "$PACKAGE"

cp "$BOOT_IMAGE" "$DIST/ZenovOS-$RELEASE_ID-x86.img"
cp "$DIST/ZenovOS-$RELEASE_ID-x86.img" "$PACKAGE/"
cp "$DATA_IMAGE" "$PACKAGE/ZenovOS-$RELEASE_ID-data.img"
cp "$ROOT/packaging/INSTALL.txt" "$DIST/INSTALL.txt"
cp "$ROOT/packaging/INSTALL.txt" "$PACKAGE/INSTALL.txt"
cp "$ROOT/packaging/run-qemu.sh" "$PACKAGE/run-qemu.sh"
cp "$ROOT/packaging/run-qemu.cmd" "$PACKAGE/run-qemu.cmd"
chmod +x "$PACKAGE/run-qemu.sh"

(
  cd "$PACKAGE"
  sha256sum "ZenovOS-$RELEASE_ID-x86.img" "ZenovOS-$RELEASE_ID-data.img" > IMAGE-SHA256SUMS.txt
)

# Fixed timestamps and stripped ZIP metadata make repeated packages byte-identical.
TZ=UTC touch -t 202607160000 \
  "$PACKAGE/ZenovOS-$RELEASE_ID-x86.img" \
  "$PACKAGE/ZenovOS-$RELEASE_ID-data.img" \
  "$PACKAGE/INSTALL.txt" \
  "$PACKAGE/IMAGE-SHA256SUMS.txt" \
  "$PACKAGE/run-qemu.sh" \
  "$PACKAGE/run-qemu.cmd"

(
  cd "$PACKAGE"
  zip -X -9 "$ROOT/$DIST/ZenovOS-$RELEASE_ID-x86.zip" \
    "ZenovOS-$RELEASE_ID-x86.img" "ZenovOS-$RELEASE_ID-data.img" INSTALL.txt \
    IMAGE-SHA256SUMS.txt run-qemu.sh run-qemu.cmd
)

(
  cd "$DIST"
  sha256sum "ZenovOS-$RELEASE_ID-x86.img" "ZenovOS-$RELEASE_ID-x86.zip" > SHA256SUMS.txt
)

unzip -Z1 "$DIST/ZenovOS-$RELEASE_ID-x86.zip" | sort > /tmp/zenov-package-files.txt
printf '%s\n' IMAGE-SHA256SUMS.txt INSTALL.txt "ZenovOS-$RELEASE_ID-data.img" "ZenovOS-$RELEASE_ID-x86.img" run-qemu.cmd run-qemu.sh | sort > /tmp/zenov-package-expected.txt
diff -u /tmp/zenov-package-expected.txt /tmp/zenov-package-files.txt
unzip -p "$DIST/ZenovOS-$RELEASE_ID-x86.zip" INSTALL.txt | cmp - "$ROOT/packaging/INSTALL.txt"
unzip -p "$DIST/ZenovOS-$RELEASE_ID-x86.zip" run-qemu.sh | cmp - "$ROOT/packaging/run-qemu.sh"
unzip -p "$DIST/ZenovOS-$RELEASE_ID-x86.zip" run-qemu.cmd | cmp - "$ROOT/packaging/run-qemu.cmd"
unzip -p "$DIST/ZenovOS-$RELEASE_ID-x86.zip" "ZenovOS-$RELEASE_ID-data.img" | cmp - "$DATA_IMAGE"
unzip -p "$DIST/ZenovOS-$RELEASE_ID-x86.zip" "ZenovOS-$RELEASE_ID-x86.img" | cmp - "$BOOT_IMAGE"

echo "package-release: OK version=$VERSION revision=$REVISION boot=$DIST/ZenovOS-$RELEASE_ID-x86.img data-in-zip=$DATA_IMAGE zip=$DIST/ZenovOS-$RELEASE_ID-x86.zip"
