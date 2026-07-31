#!/usr/bin/env bash
set -euo pipefail

BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
DIST="${3:-dist}"
PACKAGE="${4:-package}"
MANIFEST="${5:-build/build-manifest.json}"
ISO_IMAGE="${6:-build/ZenovOS-0.1.1-x86.iso}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.1.1"
SOURCE_REVISION="${ZENOV_SOURCE_REVISION:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)}"
REQUIRE_ISO="${ZENOV_REQUIRE_ISO:-0}"
ISO_ENABLED=0

for tool in zip unzip sha256sum git cmp stat; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "package-release: required tool not found: $tool" >&2
    exit 1
  }
done
[[ "$REQUIRE_ISO" == 0 || "$REQUIRE_ISO" == 1 ]] || {
  echo "package-release: ZENOV_REQUIRE_ISO must be 0 or 1" >&2
  exit 1
}

[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ ]] || {
  echo "package-release: source revision must be an exact lowercase 40-hex commit SHA" >&2
  exit 1
}
if [[ -n "${ZENOV_SOURCE_REVISION:-}" ]]; then
  git -C "$ROOT" cat-file -e "$SOURCE_REVISION^{commit}" 2>/dev/null || {
    echo "package-release: supplied source revision is not available in this checkout: $SOURCE_REVISION" >&2
    exit 1
  }
fi
[[ -f "$BOOT_IMAGE" ]] || { echo "package-release: boot image not found: $BOOT_IMAGE" >&2; exit 1; }
[[ -f "$DATA_IMAGE" ]] || { echo "package-release: data image not found: $DATA_IMAGE" >&2; exit 1; }
[[ -f "$MANIFEST" ]] || { echo "package-release: build manifest not found: $MANIFEST" >&2; exit 1; }
[[ "$(stat -c%s "$BOOT_IMAGE")" -eq 1474560 ]] || {
  echo "package-release: expected a 1,474,560-byte boot image" >&2
  exit 1
}
[[ "$(stat -c%s "$DATA_IMAGE")" -eq 16777216 ]] || {
  echo "package-release: expected a 16,777,216-byte data image" >&2
  exit 1
}
grep -q '"version": "0.1.1"' "$MANIFEST"
grep -q '"zenov_repository_commit": "a58c3419b09d46be7fc7180ba910c14033910fdf"' "$MANIFEST"
grep -q '"zenov_app_contract_sha256": "9e1733af56a53ae31055b448f762815ba7a5e1a72be543aef325bd4ea36e0ad5"' "$MANIFEST"

if [[ -f "$ISO_IMAGE" ]]; then
  command -v xorriso >/dev/null 2>&1 || {
    echo "package-release: ISO exists but xorriso is unavailable for verification" >&2
    exit 1
  }
  [[ -s "$ISO_IMAGE" ]] && [[ $(( $(stat -c%s "$ISO_IMAGE") % 2048 )) -eq 0 ]] || {
    echo "package-release: ISO must be non-empty and 2048-byte aligned" >&2
    exit 1
  }
  iso_report="$(xorriso -indev "$ISO_IMAGE" -report_el_torito plain 2>/dev/null)"
  grep -Fq 'El Torito catalog' <<<"$iso_report"
  grep -Eq 'BIOS|0x00' <<<"$iso_report"
  grep -Eiq 'floppy|fd|1\.44' <<<"$iso_report"
  tmp_extract="$(mktemp)"
  trap 'rm -f "$tmp_extract"' EXIT
  xorriso -osirrox on -indev "$ISO_IMAGE" \
    -extract /BOOT/ZENOVOS.IMG "$tmp_extract" >/dev/null 2>&1
  cmp "$BOOT_IMAGE" "$tmp_extract"
  ISO_ENABLED=1
elif [[ "$REQUIRE_ISO" == 1 ]]; then
  echo "package-release: required ISO image not found: $ISO_IMAGE" >&2
  exit 1
fi

rm -rf "$DIST" "$PACKAGE"
mkdir -p "$DIST" "$PACKAGE"

cp "$BOOT_IMAGE" "$DIST/ZenovOS-$VERSION-x86.img"
cp "$DATA_IMAGE" "$DIST/ZenovOS-$VERSION-data.img"
cp "$DIST/ZenovOS-$VERSION-x86.img" "$PACKAGE/"
cp "$DIST/ZenovOS-$VERSION-data.img" "$PACKAGE/"
if [[ "$ISO_ENABLED" == 1 ]]; then
  cp "$ISO_IMAGE" "$DIST/ZenovOS-$VERSION-x86.iso"
  cp "$DIST/ZenovOS-$VERSION-x86.iso" "$PACKAGE/"
fi
cp "$MANIFEST" "$DIST/BUILD-MANIFEST.json"
cp "$MANIFEST" "$PACKAGE/BUILD-MANIFEST.json"
cp "$ROOT/packaging/INSTALL.txt" "$DIST/INSTALL.txt"
cp "$ROOT/packaging/INSTALL.txt" "$PACKAGE/INSTALL.txt"
cp "$ROOT/packaging/run-qemu.sh" "$PACKAGE/run-qemu.sh"
cp "$ROOT/packaging/run-qemu.cmd" "$PACKAGE/run-qemu.cmd"
printf '%s\n' "$SOURCE_REVISION" > "$DIST/SOURCE-REVISION.txt"
cp "$DIST/SOURCE-REVISION.txt" "$PACKAGE/SOURCE-REVISION.txt"
chmod +x "$PACKAGE/run-qemu.sh"

package_hash_files=(
  "ZenovOS-$VERSION-x86.img"
  "ZenovOS-$VERSION-data.img"
  BUILD-MANIFEST.json
  SOURCE-REVISION.txt
)
zip_files=(
  "ZenovOS-$VERSION-x86.img"
  "ZenovOS-$VERSION-data.img"
  BUILD-MANIFEST.json
  SOURCE-REVISION.txt
  INSTALL.txt
  IMAGE-SHA256SUMS.txt
  run-qemu.sh
  run-qemu.cmd
)
if [[ "$ISO_ENABLED" == 1 ]]; then
  package_hash_files=("ZenovOS-$VERSION-x86.iso" "${package_hash_files[@]}")
  zip_files=("ZenovOS-$VERSION-x86.iso" "${zip_files[@]}")
fi
(
  cd "$PACKAGE"
  sha256sum "${package_hash_files[@]}" > IMAGE-SHA256SUMS.txt
)

stamp_files=(
  "$PACKAGE/ZenovOS-$VERSION-x86.img"
  "$PACKAGE/ZenovOS-$VERSION-data.img"
  "$PACKAGE/BUILD-MANIFEST.json"
  "$PACKAGE/SOURCE-REVISION.txt"
  "$PACKAGE/INSTALL.txt"
  "$PACKAGE/IMAGE-SHA256SUMS.txt"
  "$PACKAGE/run-qemu.sh"
  "$PACKAGE/run-qemu.cmd"
)
if [[ "$ISO_ENABLED" == 1 ]]; then
  stamp_files=("$PACKAGE/ZenovOS-$VERSION-x86.iso" "${stamp_files[@]}")
fi
TZ=UTC touch -t 202607160000 "${stamp_files[@]}"

(
  cd "$PACKAGE"
  zip -X -9 "$ROOT/$DIST/ZenovOS-$VERSION-x86.zip" "${zip_files[@]}"
)

dist_hash_files=(
  "ZenovOS-$VERSION-x86.img"
  "ZenovOS-$VERSION-data.img"
  "ZenovOS-$VERSION-x86.zip"
  BUILD-MANIFEST.json
  SOURCE-REVISION.txt
  INSTALL.txt
)
if [[ "$ISO_ENABLED" == 1 ]]; then
  dist_hash_files=("ZenovOS-$VERSION-x86.iso" "${dist_hash_files[@]}")
fi
(
  cd "$DIST"
  sha256sum "${dist_hash_files[@]}" > SHA256SUMS.txt
)

unzip -Z1 "$DIST/ZenovOS-$VERSION-x86.zip" | sort > /tmp/zenov-package-files.txt
printf '%s\n' "${zip_files[@]}" | sort > /tmp/zenov-package-expected.txt
diff -u /tmp/zenov-package-expected.txt /tmp/zenov-package-files.txt

unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" INSTALL.txt | cmp - "$ROOT/packaging/INSTALL.txt"
unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" run-qemu.sh | cmp - "$ROOT/packaging/run-qemu.sh"
unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" run-qemu.cmd | cmp - "$ROOT/packaging/run-qemu.cmd"
unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" BUILD-MANIFEST.json | cmp - "$MANIFEST"
unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" SOURCE-REVISION.txt | cmp - "$DIST/SOURCE-REVISION.txt"
unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" "ZenovOS-$VERSION-data.img" | cmp - "$DATA_IMAGE"
unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" "ZenovOS-$VERSION-x86.img" | cmp - "$BOOT_IMAGE"
if [[ "$ISO_ENABLED" == 1 ]]; then
  unzip -p "$DIST/ZenovOS-$VERSION-x86.zip" "ZenovOS-$VERSION-x86.iso" | cmp - "$ISO_IMAGE"
fi
grep -qx "$SOURCE_REVISION" "$DIST/SOURCE-REVISION.txt"
(cd "$DIST" && sha256sum -c SHA256SUMS.txt)

printf 'package-release: OK version=%s source=%s iso=%s boot=%s data=%s manifest=%s zip=%s\n' \
  "$VERSION" "$SOURCE_REVISION" "$ISO_ENABLED" \
  "$DIST/ZenovOS-$VERSION-x86.img" "$DIST/ZenovOS-$VERSION-data.img" \
  "$MANIFEST" "$DIST/ZenovOS-$VERSION-x86.zip"
