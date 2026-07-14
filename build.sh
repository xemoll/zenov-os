#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
RUN_TESTS=0
CLEAN=0

usage() {
  cat <<'EOF'
Usage: ./build.sh [--clean] [--test]

Builds the ZenovOS FAT12 image from kernel/main.zv.
  --clean  remove the previous build directory first
  --test   run compiler tests and the QEMU boot smoke
EOF
}

log() { printf '[zenov-os] %s\n' "$*"; }
fail() { printf '[zenov-os] error: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "missing dependency: $1"; }

while (($#)); do
  case "$1" in
    --clean) CLEAN=1 ;;
    --test) RUN_TESTS=1 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; fail "unknown argument: $1" ;;
  esac
  shift
done

for tool in python3 as ld sha256sum stat; do need "$tool"; done
if ((RUN_TESTS)); then need qemu-system-i386; fi

if ((CLEAN)); then rm -rf "$BUILD_DIR"; fi
mkdir -p "$BUILD_DIR"

log "testing deterministic stage0 compiler"
python3 "$ROOT/tests/test_stage0_compiler.py"

log "compiling kernel/main.zv -> kernel.generated.asm"
python3 "$ROOT/tools/zenov_baremetal_bootstrap.py" \
  "$ROOT/kernel/main.zv" \
  -o "$BUILD_DIR/kernel.generated.asm"

log "assembling bootloader and generated kernel with GNU binutils"
as --32 "$ROOT/boot/boot.S" -o "$BUILD_DIR/boot.o"
ld -m elf_i386 -T "$ROOT/boot/flat16.ld" "$BUILD_DIR/boot.o" -o "$BUILD_DIR/BOOT.BIN"
(
  cd "$ROOT"
  as --32 -I "$ROOT" "$BUILD_DIR/kernel.generated.asm" -o "$BUILD_DIR/kernel.o"
)
ld -m elf_i386 -T "$ROOT/boot/flat16.ld" "$BUILD_DIR/kernel.o" -o "$BUILD_DIR/KERNEL.BIN"

boot_size="$(stat -c%s "$BUILD_DIR/BOOT.BIN")"
kernel_size="$(stat -c%s "$BUILD_DIR/KERNEL.BIN")"
[[ "$boot_size" -eq 512 ]] || fail "boot sector is $boot_size bytes, expected 512"
((kernel_size > 0)) || fail "generated kernel is empty"
((kernel_size <= 43008)) || fail "kernel exceeds loader window: $kernel_size > 43008"

log "creating deterministic FAT12 image"
python3 "$ROOT/tools/build_fat12_image.py" \
  --boot "$BUILD_DIR/BOOT.BIN" \
  --kernel "$BUILD_DIR/KERNEL.BIN" \
  --output "$BUILD_DIR/zenov-os.img"

python3 - "$ROOT" "$BUILD_DIR" "$kernel_size" <<'PY'
from __future__ import annotations
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
build = pathlib.Path(sys.argv[2])
kernel_size = int(sys.argv[3])

def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

manifest = {
    "format": "zenov-os-build-v1",
    "product": "ZenovOS",
    "language": "Zenov",
    "source_extension": ".zv",
    "profile": "x86-bios-fat12-stage0",
    "inputs": {
        "kernel/main.zv": digest(root / "kernel/main.zv"),
        "boot/boot.S": digest(root / "boot/boot.S"),
        "runtime/kernel_runtime.inc": digest(root / "runtime/kernel_runtime.inc"),
        "tools/zenov_baremetal_bootstrap.py": digest(root / "tools/zenov_baremetal_bootstrap.py"),
    },
    "outputs": {
        "BOOT.BIN": {"bytes": (build / "BOOT.BIN").stat().st_size, "sha256": digest(build / "BOOT.BIN")},
        "KERNEL.BIN": {"bytes": kernel_size, "sha256": digest(build / "KERNEL.BIN")},
        "zenov-os.img": {"bytes": (build / "zenov-os.img").stat().st_size, "sha256": digest(build / "zenov-os.img")},
    },
}
(build / "build-manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY

log "image: $BUILD_DIR/zenov-os.img"
log "kernel: $kernel_size bytes"
log "manifest: $BUILD_DIR/build-manifest.json"

if ((RUN_TESTS)); then
  log "running QEMU boot smoke"
  python3 "$ROOT/tests/qemu_boot_smoke.py" \
    --qemu qemu-system-i386 \
    --image "$BUILD_DIR/zenov-os.img" \
    --out "$BUILD_DIR/qemu"
fi
