#!/usr/bin/env bash
set -euo pipefail

RAW_DATA_IMAGE="${1:-build/zenov-data.img}"
ISO_IMAGE="${2:-build/ZenovOS-0.1.1-x86.iso}"
APPLIANCE_DIR="${3:-build/vm-appliances}"
VERSION="0.1.1"
EXPECTED_BYTES=16777216

for tool in qemu-img python3 sha256sum cmp stat mktemp; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "verify-vm-appliances: required tool not found: $tool" >&2
    exit 1
  }
done

[[ -f "$RAW_DATA_IMAGE" ]] || { echo "verify-vm-appliances: missing raw data image" >&2; exit 1; }
[[ -f "$ISO_IMAGE" ]] || { echo "verify-vm-appliances: missing ISO image" >&2; exit 1; }
[[ "$(stat -c%s "$RAW_DATA_IMAGE")" -eq "$EXPECTED_BYTES" ]] || {
  echo "verify-vm-appliances: canonical data image size mismatch" >&2
  exit 1
}

expected=(
  "ZenovOS-$VERSION-data.qcow2"
  "ZenovOS-$VERSION-data.vdi"
  "ZenovOS-$VERSION-data.vmdk"
  "ZenovOS-$VERSION.vmx"
  VM-APPLIANCE-MANIFEST.json
  VM-QUICKSTART.txt
  VM-SHA256SUMS.txt
  prepare-vm.sh
  prepare-vm.ps1
)
for name in "${expected[@]}"; do
  [[ -s "$APPLIANCE_DIR/$name" ]] || {
    echo "verify-vm-appliances: missing or empty artifact: $name" >&2
    exit 1
  }
done

(
  cd "$APPLIANCE_DIR"
  sha256sum -c VM-SHA256SUMS.txt
)

raw_sha="$(sha256sum "$RAW_DATA_IMAGE" | cut -d' ' -f1)"
iso_sha="$(sha256sum "$ISO_IMAGE" | cut -d' ' -f1)"
export APPLIANCE_DIR VERSION raw_sha iso_sha EXPECTED_BYTES
python3 - <<'PY'
import json
import os
from pathlib import Path

path = Path(os.environ["APPLIANCE_DIR"]) / "VM-APPLIANCE-MANIFEST.json"
data = json.loads(path.read_text(encoding="utf-8"))
assert data["schema"] == 1
assert data["version"] == os.environ["VERSION"]
assert data["canonical_data_sha256"] == os.environ["raw_sha"]
assert data["iso_sha256"] == os.environ["iso_sha"]
assert data["virtual_size"] == int(os.environ["EXPECTED_BYTES"])
assert [(a["name"], a["format"]) for a in data["artifacts"]] == [
    (f"ZenovOS-{os.environ['VERSION']}-data.qcow2", "qcow2"),
    (f"ZenovOS-{os.environ['VERSION']}-data.vdi", "vdi"),
    (f"ZenovOS-{os.environ['VERSION']}-data.vmdk", "vmdk"),
]
PY

for spec in \
  "qcow2:$APPLIANCE_DIR/ZenovOS-$VERSION-data.qcow2" \
  "vdi:$APPLIANCE_DIR/ZenovOS-$VERSION-data.vdi" \
  "vmdk:$APPLIANCE_DIR/ZenovOS-$VERSION-data.vmdk"; do
  format="${spec%%:*}"
  image="${spec#*:}"
  qemu-img check -q -f "$format" "$image"
  info="$(qemu-img info --output=json -f "$format" "$image")"
  actual_format="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["format"])' <<<"$info")"
  virtual_size="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["virtual-size"])' <<<"$info")"
  [[ "$actual_format" == "$format" ]] || {
    echo "verify-vm-appliances: expected $format, got $actual_format" >&2
    exit 1
  }
  [[ "$virtual_size" -eq "$EXPECTED_BYTES" ]] || {
    echo "verify-vm-appliances: virtual size mismatch for $image" >&2
    exit 1
  }
  roundtrip="$(mktemp)"
  qemu-img convert -q -f "$format" -O raw "$image" "$roundtrip"
  cmp "$RAW_DATA_IMAGE" "$roundtrip"
  rm -f "$roundtrip"
done

grep -Fq 'firmware = "bios"' "$APPLIANCE_DIR/ZenovOS-$VERSION.vmx"
grep -Fq "ZenovOS-$VERSION-data.vmdk" "$APPLIANCE_DIR/ZenovOS-$VERSION.vmx"
grep -Fq "ZenovOS-$VERSION-x86.iso" "$APPLIANCE_DIR/ZenovOS-$VERSION.vmx"
bash -n "$APPLIANCE_DIR/prepare-vm.sh"

printf 'verify-vm-appliances: OK version=%s raw_sha256=%s iso_sha256=%s formats=qcow2,vdi,vmdk\n' \
  "$VERSION" "$raw_sha" "$iso_sha"
