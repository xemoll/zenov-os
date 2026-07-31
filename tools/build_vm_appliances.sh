#!/usr/bin/env bash
set -euo pipefail
umask 022

RAW_DATA_IMAGE="${1:-build/zenov-data.img}"
ISO_IMAGE="${2:-build/ZenovOS-0.1.1-x86.iso}"
OUT="${3:-build/vm-appliances}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.1.1"
EXPECTED_BYTES=16777216

for tool in qemu-img python3 sha256sum stat cmp mktemp install; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "build-vm-appliances: required tool not found: $tool" >&2
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

RAW_ABS="$(absolute_path "$RAW_DATA_IMAGE")"
ISO_ABS="$(absolute_path "$ISO_IMAGE")"
OUT_ABS="$(absolute_path "$OUT")"
HOME_ABS="$(absolute_path "${HOME:-/nonexistent}")"

[[ -f "$RAW_ABS" ]] || {
  echo "build-vm-appliances: raw data image not found: $RAW_ABS" >&2
  exit 1
}
[[ -f "$ISO_ABS" && -s "$ISO_ABS" ]] || {
  echo "build-vm-appliances: ISO image not found or empty: $ISO_ABS" >&2
  exit 1
}
[[ "$(stat -c%s "$RAW_ABS")" -eq "$EXPECTED_BYTES" ]] || {
  echo "build-vm-appliances: expected a $EXPECTED_BYTES-byte ZenovFS data image" >&2
  exit 1
}

case "$OUT_ABS" in
  /|"$ROOT"|"$HOME_ABS")
    echo "build-vm-appliances: refusing unsafe output directory: $OUT_ABS" >&2
    exit 1
    ;;
esac
case "$RAW_ABS" in "$OUT_ABS"|"$OUT_ABS"/*)
  echo "build-vm-appliances: output directory contains the canonical data image" >&2
  exit 1
esac
case "$ISO_ABS" in "$OUT_ABS"|"$OUT_ABS"/*)
  echo "build-vm-appliances: output directory contains the ISO image" >&2
  exit 1
esac
[[ ! -L "$OUT_ABS" ]] || {
  echo "build-vm-appliances: refusing symlink output directory: $OUT_ABS" >&2
  exit 1
}

rm -rf -- "$OUT_ABS"
mkdir -p -- "$OUT_ABS"
OUT="$OUT_ABS"

QCOW2="$OUT/ZenovOS-$VERSION-data.qcow2"
VDI="$OUT/ZenovOS-$VERSION-data.vdi"
VMDK="$OUT/ZenovOS-$VERSION-data.vmdk"

# The raw ZenovFS image remains the canonical content authority. The converted
# containers are convenience transports for the major desktop hypervisors.
qemu-img convert -q -f raw -O qcow2 \
  -o compat=1.1,cluster_size=65536,lazy_refcounts=off \
  "$RAW_ABS" "$QCOW2"
qemu-img convert -q -f raw -O vdi "$RAW_ABS" "$VDI"
qemu-img convert -q -f raw -O vmdk \
  -o subformat=monolithicSparse,compat6 \
  "$RAW_ABS" "$VMDK"

verify_container() {
  local format="$1" image="$2" info actual_format virtual_size roundtrip
  [[ -s "$image" ]] || {
    echo "build-vm-appliances: empty converted image: $image" >&2
    return 1
  }
  qemu-img check -q -f "$format" "$image"
  info="$(qemu-img info --output=json -f "$format" "$image")"
  actual_format="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["format"])' <<<"$info")"
  virtual_size="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["virtual-size"])' <<<"$info")"
  [[ "$actual_format" == "$format" ]] || {
    echo "build-vm-appliances: expected $format, got $actual_format for $image" >&2
    return 1
  }
  [[ "$virtual_size" -eq "$EXPECTED_BYTES" ]] || {
    echo "build-vm-appliances: virtual size mismatch for $image: $virtual_size" >&2
    return 1
  }
  roundtrip="$(mktemp "${TMPDIR:-/tmp}/zenov-vm-roundtrip.XXXXXX")"
  if ! qemu-img convert -q -f "$format" -O raw "$image" "$roundtrip" \
      || ! cmp "$RAW_ABS" "$roundtrip"; then
    rm -f -- "$roundtrip"
    echo "build-vm-appliances: guest-visible content mismatch for $image" >&2
    return 1
  fi
  rm -f -- "$roundtrip"
}

verify_container qcow2 "$QCOW2"
verify_container vdi "$VDI"
verify_container vmdk "$VMDK"

install -m 0755 "$ROOT/packaging/prepare-vm.sh" "$OUT/prepare-vm.sh"
install -m 0644 "$ROOT/packaging/prepare-vm.ps1" "$OUT/prepare-vm.ps1"
install -m 0644 "$ROOT/packaging/ZenovOS-0.1.1.vmx" "$OUT/ZenovOS-0.1.1.vmx"
install -m 0644 "$ROOT/packaging/VM-QUICKSTART.txt" "$OUT/VM-QUICKSTART.txt"

raw_sha="$(sha256sum "$RAW_ABS" | cut -d' ' -f1)"
iso_sha="$(sha256sum "$ISO_ABS" | cut -d' ' -f1)"
export OUT VERSION RAW_ABS ISO_ABS raw_sha iso_sha EXPECTED_BYTES
python3 - <<'PY'
import json
import os
from pathlib import Path

out = Path(os.environ["OUT"])
manifest = {
    "schema": 1,
    "version": os.environ["VERSION"],
    "canonical_data_image": Path(os.environ["RAW_ABS"]).name,
    "canonical_data_sha256": os.environ["raw_sha"],
    "iso_image": Path(os.environ["ISO_ABS"]).name,
    "iso_sha256": os.environ["iso_sha"],
    "virtual_size": int(os.environ["EXPECTED_BYTES"]),
    "reproducibility": {
        "container_bytes": "format metadata may differ between qemu-img builds",
        "guest_content": "every generated image is converted back to raw and compared byte-for-byte"
    },
    "artifacts": [
        {"name": f"ZenovOS-{os.environ['VERSION']}-data.qcow2", "format": "qcow2"},
        {"name": f"ZenovOS-{os.environ['VERSION']}-data.vdi", "format": "vdi"},
        {"name": f"ZenovOS-{os.environ['VERSION']}-data.vmdk", "format": "vmdk"}
    ]
}
(out / "VM-APPLIANCE-MANIFEST.json").write_text(
    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
    encoding="utf-8"
)
PY

(
  cd "$OUT"
  sha256sum \
    "ZenovOS-$VERSION-data.qcow2" \
    "ZenovOS-$VERSION-data.vdi" \
    "ZenovOS-$VERSION-data.vmdk" \
    VM-APPLIANCE-MANIFEST.json \
    VM-QUICKSTART.txt \
    prepare-vm.sh \
    prepare-vm.ps1 \
    "ZenovOS-$VERSION.vmx" > VM-SHA256SUMS.txt
)

printf 'build-vm-appliances: OK version=%s raw_sha256=%s iso_sha256=%s out=%s\n' \
  "$VERSION" "$raw_sha" "$iso_sha" "$OUT"
