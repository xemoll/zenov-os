#!/usr/bin/env bash
set -euo pipefail
umask 022

BOOT_IMAGE="${1:-build/zenov-os.img}"
RAW_DATA_IMAGE="${2:-build/zenov-data.img}"
ISO_IMAGE="${3:-build/ZenovOS-0.1.1-x86.iso}"
APPLIANCE_DIR="${4:-build/vm-appliances}"
DIST="${5:-dist-vm}"
BUILD_MANIFEST="${6:-build/build-manifest.json}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.1.1"
SOURCE_REVISION="${ZENOV_SOURCE_REVISION:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)}"

for tool in sha256sum git stat cmp python3 install; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "package-vm-appliances: required tool not found: $tool" >&2
    exit 1
  }
done

absolute_path() {
  python3 - "$1" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
}

BOOT_ABS="$(absolute_path "$BOOT_IMAGE")"
RAW_ABS="$(absolute_path "$RAW_DATA_IMAGE")"
ISO_ABS="$(absolute_path "$ISO_IMAGE")"
APPLIANCE_ABS="$(absolute_path "$APPLIANCE_DIR")"
BUILD_MANIFEST_ABS="$(absolute_path "$BUILD_MANIFEST")"
DIST_ABS="$(absolute_path "$DIST")"
HOME_ABS="$(absolute_path "${HOME:-/nonexistent}")"

[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]] || {
  echo "package-vm-appliances: source revision must be an exact lowercase 40-hex SHA" >&2
  exit 1
}
[[ -s "$BOOT_ABS" ]] || { echo "package-vm-appliances: missing boot image" >&2; exit 1; }
[[ -s "$RAW_ABS" ]] || { echo "package-vm-appliances: missing data image" >&2; exit 1; }
[[ -s "$ISO_ABS" ]] || { echo "package-vm-appliances: missing ISO image" >&2; exit 1; }
[[ -s "$BUILD_MANIFEST_ABS" ]] || { echo "package-vm-appliances: missing build manifest" >&2; exit 1; }
[[ "$(stat -c%s "$BOOT_ABS")" -eq 1474560 ]] || { echo "package-vm-appliances: invalid boot image size" >&2; exit 1; }
[[ "$(stat -c%s "$RAW_ABS")" -eq 16777216 ]] || { echo "package-vm-appliances: invalid data image size" >&2; exit 1; }

case "$DIST_ABS" in
  /|"$ROOT"|"$HOME_ABS")
    echo "package-vm-appliances: refusing unsafe distribution directory: $DIST_ABS" >&2
    exit 1
    ;;
esac
for protected in "$BOOT_ABS" "$RAW_ABS" "$ISO_ABS" "$APPLIANCE_ABS" "$BUILD_MANIFEST_ABS"; do
  case "$protected" in "$DIST_ABS"|"$DIST_ABS"/*)
    echo "package-vm-appliances: distribution directory contains an input: $protected" >&2
    exit 1
  esac
done
[[ ! -L "$DIST_ABS" ]] || {
  echo "package-vm-appliances: refusing symlink distribution directory: $DIST_ABS" >&2
  exit 1
}

bash "$ROOT/tools/verify_vm_appliances.sh" "$RAW_ABS" "$ISO_ABS" "$APPLIANCE_ABS"

rm -rf -- "$DIST_ABS"
mkdir -p -- "$DIST_ABS"
DIST="$DIST_ABS"

install -m 0644 "$ISO_ABS" "$DIST/ZenovOS-$VERSION-x86.iso"
install -m 0644 "$BOOT_ABS" "$DIST/ZenovOS-$VERSION-x86.img"
install -m 0644 "$RAW_ABS" "$DIST/ZenovOS-$VERSION-data.img"
install -m 0644 "$APPLIANCE_ABS/ZenovOS-$VERSION-data.qcow2" "$DIST/"
install -m 0644 "$APPLIANCE_ABS/ZenovOS-$VERSION-data.vdi" "$DIST/"
install -m 0644 "$APPLIANCE_ABS/ZenovOS-$VERSION-data.vmdk" "$DIST/"
install -m 0644 "$APPLIANCE_ABS/ZenovOS-$VERSION.vmx" "$DIST/"
install -m 0755 "$APPLIANCE_ABS/prepare-vm.sh" "$DIST/"
install -m 0644 "$APPLIANCE_ABS/prepare-vm.ps1" "$DIST/"
install -m 0644 "$APPLIANCE_ABS/VM-QUICKSTART.txt" "$DIST/"
install -m 0644 "$APPLIANCE_ABS/VM-APPLIANCE-MANIFEST.json" "$DIST/"
install -m 0644 "$BUILD_MANIFEST_ABS" "$DIST/BUILD-MANIFEST.json"
printf '%s\n' "$SOURCE_REVISION" > "$DIST/SOURCE-REVISION.txt"

(
  cd "$DIST"
  sha256sum \
    "ZenovOS-$VERSION-x86.iso" \
    "ZenovOS-$VERSION-x86.img" \
    "ZenovOS-$VERSION-data.img" \
    "ZenovOS-$VERSION-data.qcow2" \
    "ZenovOS-$VERSION-data.vdi" \
    "ZenovOS-$VERSION-data.vmdk" \
    "ZenovOS-$VERSION.vmx" \
    prepare-vm.sh \
    prepare-vm.ps1 \
    VM-QUICKSTART.txt \
    VM-APPLIANCE-MANIFEST.json \
    BUILD-MANIFEST.json \
    SOURCE-REVISION.txt > SHA256SUMS.txt
  sha256sum -c SHA256SUMS.txt
)

cmp "$DIST/ZenovOS-$VERSION-x86.iso" "$ISO_ABS"
cmp "$DIST/ZenovOS-$VERSION-x86.img" "$BOOT_ABS"
cmp "$DIST/ZenovOS-$VERSION-data.img" "$RAW_ABS"
grep -qx "$SOURCE_REVISION" "$DIST/SOURCE-REVISION.txt"
[[ "$(find "$DIST" -maxdepth 1 -type f | wc -l)" -eq 14 ]] || {
  echo "package-vm-appliances: unexpected direct asset count" >&2
  exit 1
}

printf 'package-vm-appliances: OK version=%s source=%s assets=14 dist=%s\n' \
  "$VERSION" "$SOURCE_REVISION" "$DIST"
