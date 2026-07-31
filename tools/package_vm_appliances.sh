#!/usr/bin/env bash
set -euo pipefail

BOOT_IMAGE="${1:-build/zenov-os.img}"
RAW_DATA_IMAGE="${2:-build/zenov-data.img}"
ISO_IMAGE="${3:-build/ZenovOS-0.1.1-x86.iso}"
APPLIANCE_DIR="${4:-build/vm-appliances}"
DIST="${5:-dist-vm}"
BUILD_MANIFEST="${6:-build/build-manifest.json}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.1.1"
SOURCE_REVISION="${ZENOV_SOURCE_REVISION:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)}"

for tool in sha256sum git stat cmp; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "package-vm-appliances: required tool not found: $tool" >&2
    exit 1
  }
done
[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]] || {
  echo "package-vm-appliances: source revision must be an exact lowercase 40-hex SHA" >&2
  exit 1
}
[[ -s "$BOOT_IMAGE" ]] || { echo "package-vm-appliances: missing boot image" >&2; exit 1; }
[[ -s "$RAW_DATA_IMAGE" ]] || { echo "package-vm-appliances: missing data image" >&2; exit 1; }
[[ -s "$ISO_IMAGE" ]] || { echo "package-vm-appliances: missing ISO image" >&2; exit 1; }
[[ -s "$BUILD_MANIFEST" ]] || { echo "package-vm-appliances: missing build manifest" >&2; exit 1; }
[[ "$(stat -c%s "$BOOT_IMAGE")" -eq 1474560 ]] || { echo "package-vm-appliances: invalid boot image size" >&2; exit 1; }
[[ "$(stat -c%s "$RAW_DATA_IMAGE")" -eq 16777216 ]] || { echo "package-vm-appliances: invalid data image size" >&2; exit 1; }

bash "$ROOT/tools/verify_vm_appliances.sh" "$RAW_DATA_IMAGE" "$ISO_IMAGE" "$APPLIANCE_DIR"

case "$DIST" in
  /*) ;;
  *) DIST="$ROOT/$DIST" ;;
esac
rm -rf "$DIST"
mkdir -p "$DIST"

cp "$ISO_IMAGE" "$DIST/ZenovOS-$VERSION-x86.iso"
cp "$BOOT_IMAGE" "$DIST/ZenovOS-$VERSION-x86.img"
cp "$RAW_DATA_IMAGE" "$DIST/ZenovOS-$VERSION-data.img"
cp "$APPLIANCE_DIR/ZenovOS-$VERSION-data.qcow2" "$DIST/"
cp "$APPLIANCE_DIR/ZenovOS-$VERSION-data.vdi" "$DIST/"
cp "$APPLIANCE_DIR/ZenovOS-$VERSION-data.vmdk" "$DIST/"
cp "$APPLIANCE_DIR/ZenovOS-$VERSION.vmx" "$DIST/"
cp "$APPLIANCE_DIR/prepare-vm.sh" "$DIST/"
cp "$APPLIANCE_DIR/prepare-vm.ps1" "$DIST/"
cp "$APPLIANCE_DIR/VM-QUICKSTART.txt" "$DIST/"
cp "$APPLIANCE_DIR/VM-APPLIANCE-MANIFEST.json" "$DIST/"
cp "$BUILD_MANIFEST" "$DIST/BUILD-MANIFEST.json"
printf '%s\n' "$SOURCE_REVISION" > "$DIST/SOURCE-REVISION.txt"
chmod +x "$DIST/prepare-vm.sh"

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

cmp "$DIST/ZenovOS-$VERSION-x86.iso" "$ISO_IMAGE"
cmp "$DIST/ZenovOS-$VERSION-x86.img" "$BOOT_IMAGE"
cmp "$DIST/ZenovOS-$VERSION-data.img" "$RAW_DATA_IMAGE"
grep -qx "$SOURCE_REVISION" "$DIST/SOURCE-REVISION.txt"

printf 'package-vm-appliances: OK version=%s source=%s assets=14 dist=%s\n' \
  "$VERSION" "$SOURCE_REVISION" "$DIST"
