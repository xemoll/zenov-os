#!/usr/bin/env bash
set -euo pipefail
umask 077

VERSION="0.1.1"
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
COMMAND="${1:-status}"
FORMAT="${ZENOV_VM_FORMAT:-qcow2}"
STATE_DIR="${ZENOV_VM_STATE_DIR:-$BASE_DIR/runtime}"
BACKUP_DIR="${ZENOV_VM_BACKUP_DIR:-$BASE_DIR/backups}"
SEED="$BASE_DIR/ZenovOS-$VERSION-data.img"
CHECKSUMS="$BASE_DIR/SHA256SUMS.txt"
LOCK_DIR="$STATE_DIR/.zenov-vm.lock"
EXPECTED_SIZE=16777216

usage() {
  cat <<EOF_USAGE
Usage: $(basename "$0") status|create|verify|backup|restore|reset|remove [backup-file]

Environment:
  ZENOV_VM_FORMAT      raw|qcow2|vdi|vmdk (default: qcow2)
  ZENOV_VM_STATE_DIR   writable runtime directory (default: ./runtime)
  ZENOV_VM_BACKUP_DIR  backup directory (default: ./backups)

Operations are fail-closed and transactional. Existing disks are never overwritten
in place. restore/reset first preserve the current disk as a timestamped backup.
EOF_USAGE
}

fail() { echo "manage-vm: $*" >&2; exit 1; }
require_tool() { command -v "$1" >/dev/null 2>&1 || fail "required tool not found: $1"; }

sha256_file() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print $1}'
  else
    fail "SHA-256 verification requires sha256sum or shasum"
  fi
}

case "$FORMAT" in
  raw) EXT=img ;;
  qcow2) EXT=qcow2 ;;
  vdi) EXT=vdi ;;
  vmdk) EXT=vmdk ;;
  *) fail "unsupported format: $FORMAT" ;;
esac
TARGET="$STATE_DIR/ZenovOS-$VERSION-data.$EXT"

safe_dir() {
  local path="$1" abs
  mkdir -p -- "$path"
  [[ ! -L "$path" ]] || fail "refusing symlink directory: $path"
  abs="$(cd "$path" && pwd -P)"
  case "$abs" in /|"$HOME") fail "refusing unsafe directory: $abs" ;; esac
  printf '%s\n' "$abs"
}

verify_seed() {
  local line expected actual recorded
  [[ -f "$SEED" && ! -L "$SEED" ]] || fail "missing canonical data seed: $SEED"
  [[ "$(wc -c < "$SEED" | tr -d ' ')" -eq "$EXPECTED_SIZE" ]] || fail "invalid seed size"
  [[ -f "$CHECKSUMS" && ! -L "$CHECKSUMS" ]] || fail "missing checksum manifest: $CHECKSUMS"

  line="$(grep -E "  ZenovOS-$VERSION-data\.img$" "$CHECKSUMS" || true)"
  [[ "$(printf '%s\n' "$line" | sed '/^$/d' | wc -l | tr -d ' ')" -eq 1 ]] || fail "checksum file must contain one canonical data seed entry"
  expected="$(printf '%s\n' "$line" | awk '{print $1}')"
  recorded="$(printf '%s\n' "$line" | sed -E 's/^[0-9A-Fa-f]{64}  //')"
  [[ "$expected" =~ ^[0-9A-Fa-f]{64}$ && "$recorded" == "ZenovOS-$VERSION-data.img" ]] || fail "invalid canonical data seed checksum entry"
  actual="$(sha256_file "$SEED")"
  [[ "$(printf '%s' "$actual" | tr 'A-F' 'a-f')" == "$(printf '%s' "$expected" | tr 'A-F' 'a-f')" ]] || fail "canonical data seed checksum mismatch"
}

acquire_lock() {
  safe_dir "$STATE_DIR" >/dev/null
  mkdir "$LOCK_DIR" 2>/dev/null || fail "another lifecycle operation is active: $LOCK_DIR"
  trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT
}

verify_image() {
  local image="$1" image_format="$2" virtual_size
  [[ -f "$image" && ! -L "$image" && -s "$image" ]] || fail "disk image is missing, empty or a symlink: $image"
  case "$image_format" in
    raw)
      [[ "$(wc -c < "$image" | tr -d ' ')" -eq "$EXPECTED_SIZE" ]] || fail "invalid raw runtime size: $image"
      ;;
    qcow2|vdi|vmdk)
      require_tool qemu-img
      require_tool python3
      qemu-img check -q -f "$image_format" "$image"
      virtual_size="$(qemu-img info --output=json --force-share -f "$image_format" "$image" | python3 -c 'import json,sys; print(json.load(sys.stdin)["virtual-size"])')"
      [[ "$virtual_size" -eq "$EXPECTED_SIZE" ]] || fail "invalid virtual size: $image"
      ;;
    *) fail "unsupported verification format: $image_format" ;;
  esac
}

convert_seed_to() {
  local output tmp
  output="$1"
  tmp="$output.tmp.$$"
  rm -f -- "$tmp"
  case "$FORMAT" in
    raw) cp -- "$SEED" "$tmp" ;;
    qcow2)
      require_tool qemu-img
      qemu-img convert -q -f raw -O qcow2 -o compat=1.1,cluster_size=65536 "$SEED" "$tmp"
      ;;
    vdi)
      require_tool qemu-img
      qemu-img convert -q -f raw -O vdi "$SEED" "$tmp"
      ;;
    vmdk)
      require_tool qemu-img
      qemu-img convert -q -f raw -O vmdk -o subformat=monolithicSparse,compat6 "$SEED" "$tmp"
      ;;
  esac
  chmod 600 "$tmp"
  verify_image "$tmp" "$FORMAT"
  mv -f -- "$tmp" "$output"
}

verify_target() {
  verify_image "$TARGET" "$FORMAT"
}

backup_current() {
  [[ ! -L "$TARGET" ]] || fail "refusing symlink runtime disk: $TARGET"
  [[ -e "$TARGET" ]] || return 0
  local backups stamp dest tmp sidecar_tmp sum
  verify_target
  backups="$(safe_dir "$BACKUP_DIR")"
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  dest="$backups/ZenovOS-$VERSION-data-$stamp.$EXT"
  [[ ! -e "$dest" ]] || dest="$backups/ZenovOS-$VERSION-data-$stamp-$$.$EXT"
  tmp="$dest.tmp.$$"
  sidecar_tmp="$dest.sha256.tmp.$$"
  rm -f -- "$tmp" "$sidecar_tmp"
  cp --reflink=auto --sparse=always -- "$TARGET" "$tmp"
  chmod 600 "$tmp"
  verify_image "$tmp" "$FORMAT"
  mv -- "$tmp" "$dest"
  sum="$(sha256_file "$dest")"
  printf '%s  %s\n' "$sum" "$(basename "$dest")" > "$sidecar_tmp"
  chmod 600 "$sidecar_tmp"
  mv -- "$sidecar_tmp" "$dest.sha256"
  printf '%s\n' "$dest"
}

verify_backup_checksum() {
  local source="$1" sidecar line expected recorded actual
  sidecar="$source.sha256"
  [[ -f "$sidecar" && ! -L "$sidecar" ]] || fail "missing backup checksum sidecar: $sidecar"
  line="$(cat -- "$sidecar")"
  [[ "$(printf '%s\n' "$line" | sed '/^$/d' | wc -l | tr -d ' ')" -eq 1 ]] || fail "backup checksum sidecar must contain exactly one entry"
  expected="$(printf '%s\n' "$line" | awk '{print $1}')"
  recorded="$(printf '%s\n' "$line" | sed -E 's/^[0-9A-Fa-f]{64}  //')"
  [[ "$expected" =~ ^[0-9A-Fa-f]{64}$ && "$recorded" == "$(basename "$source")" ]] || fail "invalid backup checksum sidecar"
  actual="$(sha256_file "$source")"
  [[ "$(printf '%s' "$actual" | tr 'A-F' 'a-f')" == "$(printf '%s' "$expected" | tr 'A-F' 'a-f')" ]] || fail "backup checksum mismatch: $source"
}

restore_backup() {
  local source="$1" tmp source_abs target_abs
  [[ -n "$source" && -f "$source" && ! -L "$source" && -s "$source" ]] || fail "backup file is required and must be a regular file"
  source_abs="$(cd "$(dirname "$source")" && pwd -P)/$(basename "$source")"
  target_abs="$(cd "$(dirname "$TARGET")" && pwd -P)/$(basename "$TARGET")"
  [[ "$source_abs" != "$target_abs" ]] || fail "backup source must differ from the runtime disk"
  verify_backup_checksum "$source"
  verify_image "$source" "$FORMAT"
  backup_current >/dev/null
  tmp="$TARGET.restore.$$"
  rm -f -- "$tmp"
  cp --reflink=auto --sparse=always -- "$source" "$tmp"
  chmod 600 "$tmp"
  verify_image "$tmp" "$FORMAT"
  mv -f -- "$tmp" "$TARGET"
}

verify_seed
case "$COMMAND" in
  status)
    printf 'format=%s\nstate_dir=%s\ntarget=%s\nexists=%s\n' "$FORMAT" "$STATE_DIR" "$TARGET" "$([[ -s "$TARGET" ]] && echo yes || echo no)"
    ;;
  create)
    acquire_lock
    [[ ! -e "$TARGET" && ! -L "$TARGET" ]] || fail "runtime disk already exists: $TARGET"
    convert_seed_to "$TARGET"
    verify_target
    printf 'created=%s\n' "$TARGET"
    ;;
  verify)
    verify_target
    printf 'verified=%s\n' "$TARGET"
    ;;
  backup)
    acquire_lock
    verify_target
    printf 'backup=%s\n' "$(backup_current)"
    ;;
  restore)
    acquire_lock
    restore_backup "${2:-}"
    printf 'restored=%s\n' "$TARGET"
    ;;
  reset)
    acquire_lock
    backup_current >/dev/null
    convert_seed_to "$TARGET"
    verify_target
    printf 'reset=%s\n' "$TARGET"
    ;;
  remove)
    acquire_lock
    if [[ -e "$TARGET" || -L "$TARGET" ]]; then
      verify_target
      backup_current >/dev/null
      rm -f -- "$TARGET"
    fi
    printf 'removed=%s\n' "$TARGET"
    ;;
  -h|--help|help) usage ;;
  *) usage >&2; exit 2 ;;
esac
