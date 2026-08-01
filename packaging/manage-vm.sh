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

usage() {
  cat <<EOF
Usage: $(basename "$0") status|create|verify|backup|restore|reset|remove [backup-file]

Environment:
  ZENOV_VM_FORMAT      raw|qcow2|vdi|vmdk (default: qcow2)
  ZENOV_VM_STATE_DIR   writable runtime directory (default: ./runtime)
  ZENOV_VM_BACKUP_DIR  backup directory (default: ./backups)

Operations are fail-closed and transactional. Existing disks are never overwritten
in place. restore/reset first preserve the current disk as a timestamped backup.
EOF
}

fail() { echo "manage-vm: $*" >&2; exit 1; }
require_tool() { command -v "$1" >/dev/null 2>&1 || fail "required tool not found: $1"; }

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
  [[ -s "$SEED" ]] || fail "missing canonical data seed: $SEED"
  [[ "$(wc -c < "$SEED" | tr -d ' ')" -eq 16777216 ]] || fail "invalid seed size"
  if [[ -f "$CHECKSUMS" ]]; then
    local line
    line="$(grep -E "  ZenovOS-$VERSION-data\.img$" "$CHECKSUMS" || true)"
    [[ "$(printf '%s\n' "$line" | sed '/^$/d' | wc -l | tr -d ' ')" -eq 1 ]] || fail "checksum file must contain one canonical data seed entry"
    if command -v sha256sum >/dev/null 2>&1; then
      (cd "$BASE_DIR" && printf '%s\n' "$line" | sha256sum -c - >/dev/null)
    elif command -v shasum >/dev/null 2>&1; then
      (cd "$BASE_DIR" && printf '%s\n' "$line" | shasum -a 256 -c - >/dev/null)
    else
      fail "SHA-256 verification requires sha256sum or shasum"
    fi
  fi
}

acquire_lock() {
  safe_dir "$STATE_DIR" >/dev/null
  mkdir "$LOCK_DIR" 2>/dev/null || fail "another lifecycle operation is active: $LOCK_DIR"
  trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT
}

convert_seed_to() {
  local output="$1" tmp="$output.tmp.$$"
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
  mv -f -- "$tmp" "$output"
}

verify_target() {
  [[ -s "$TARGET" ]] || fail "runtime disk does not exist: $TARGET"
  case "$FORMAT" in
    raw)
      [[ "$(wc -c < "$TARGET" | tr -d ' ')" -eq 16777216 ]] || fail "invalid raw runtime size"
      ;;
    *)
      require_tool qemu-img
      qemu-img check -q -f "$FORMAT" "$TARGET"
      [[ "$(qemu-img info --output=json --force-share -f "$FORMAT" "$TARGET" | python3 -c 'import json,sys; print(json.load(sys.stdin)["virtual-size"])')" -eq 16777216 ]] || fail "invalid virtual size"
      ;;
  esac
}

backup_current() {
  [[ -s "$TARGET" ]] || return 0
  local backups stamp dest sum
  backups="$(safe_dir "$BACKUP_DIR")"
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  dest="$backups/ZenovOS-$VERSION-data-$stamp.$EXT"
  [[ ! -e "$dest" ]] || dest="$backups/ZenovOS-$VERSION-data-$stamp-$$.$EXT"
  cp --reflink=auto --sparse=always -- "$TARGET" "$dest.tmp"
  chmod 600 "$dest.tmp"
  mv -- "$dest.tmp" "$dest"
  if command -v sha256sum >/dev/null 2>&1; then sum="$(sha256sum "$dest" | awk '{print $1}')"; else sum="$(shasum -a 256 "$dest" | awk '{print $1}')"; fi
  printf '%s  %s\n' "$sum" "$(basename "$dest")" > "$dest.sha256"
  chmod 600 "$dest.sha256"
  printf '%s\n' "$dest"
}

restore_backup() {
  local source="$1" tmp
  [[ -n "$source" && -s "$source" ]] || fail "backup file is required"
  [[ ! -L "$source" ]] || fail "refusing symlink backup"
  backup_current >/dev/null
  tmp="$TARGET.restore.$$"
  cp --reflink=auto --sparse=always -- "$source" "$tmp"
  chmod 600 "$tmp"
  mv -f -- "$tmp" "$TARGET"
  verify_target
}

verify_seed
case "$COMMAND" in
  status)
    printf 'format=%s\nstate_dir=%s\ntarget=%s\nexists=%s\n' "$FORMAT" "$STATE_DIR" "$TARGET" "$([[ -s "$TARGET" ]] && echo yes || echo no)"
    ;;
  create)
    acquire_lock
    [[ ! -e "$TARGET" ]] || fail "runtime disk already exists: $TARGET"
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
    if [[ -s "$TARGET" ]]; then backup_current >/dev/null; rm -f -- "$TARGET"; fi
    printf 'removed=%s\n' "$TARGET"
    ;;
  -h|--help|help) usage ;;
  *) usage >&2; exit 2 ;;
esac
