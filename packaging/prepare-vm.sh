#!/usr/bin/env bash
set -euo pipefail

VERSION="0.1.1"
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HYPERVISOR="${1:-qemu}"
VM_NAME="${ZENOV_VM_NAME:-ZenovOS $VERSION}"
START_VM="${ZENOV_VM_START:-1}"
RESET_DISK="${ZENOV_VM_RESET:-0}"
ISO="$BASE_DIR/ZenovOS-$VERSION-x86.iso"
RAW="$BASE_DIR/ZenovOS-$VERSION-data.img"
QCOW2="$BASE_DIR/ZenovOS-$VERSION-data.qcow2"
VDI="$BASE_DIR/ZenovOS-$VERSION-data.vdi"
VMDK="$BASE_DIR/ZenovOS-$VERSION-data.vmdk"
VMX="$BASE_DIR/ZenovOS-$VERSION.vmx"
CHECKSUMS="$BASE_DIR/SHA256SUMS.txt"

usage() {
  cat <<EOF
Usage: $(basename "$0") qemu|virtualbox|vmware

Environment:
  ZENOV_VM_NAME       VirtualBox display name (default: $VM_NAME)
  ZENOV_VM_START      1 to start after preparation, 0 to prepare only
  ZENOV_VM_RESET      1 to recreate the selected writable disk from data.img

The ISO is read-only. Persistent files, settings and packages are stored in the
selected writable data disk. Keep a backup before setting ZENOV_VM_RESET=1.
EOF
}

verify_immutable_seeds() {
  local selected count
  [[ -f "$CHECKSUMS" ]] || return 0
  selected="$(grep -E "  (ZenovOS-$VERSION-x86\.iso|ZenovOS-$VERSION-data\.img)$" "$CHECKSUMS" || true)"
  count="$(printf '%s\n' "$selected" | sed '/^$/d' | wc -l | tr -d ' ')"
  [[ "$count" -eq 2 ]] || {
    echo "Checksum file does not contain exactly the ISO and canonical data seed" >&2
    exit 1
  }
  if command -v sha256sum >/dev/null 2>&1; then
    (cd "$BASE_DIR" && printf '%s\n' "$selected" | sha256sum -c -)
  elif command -v shasum >/dev/null 2>&1; then
    (cd "$BASE_DIR" && printf '%s\n' "$selected" | shasum -a 256 -c -)
  else
    echo "SHA-256 verification requires sha256sum or shasum" >&2
    exit 1
  fi
}

case "$START_VM" in 0|1) ;; *) echo "ZENOV_VM_START must be 0 or 1" >&2; exit 2 ;; esac
case "$RESET_DISK" in 0|1) ;; *) echo "ZENOV_VM_RESET must be 0 or 1" >&2; exit 2 ;; esac
[[ -s "$ISO" ]] || { echo "Missing boot ISO: $ISO" >&2; exit 1; }
[[ -s "$RAW" ]] || { echo "Missing canonical data image: $RAW" >&2; exit 1; }
verify_immutable_seeds

prepare_qemu() {
  command -v qemu-img >/dev/null 2>&1 || { echo "qemu-img was not found" >&2; exit 1; }
  command -v qemu-system-i386 >/dev/null 2>&1 || { echo "qemu-system-i386 was not found" >&2; exit 1; }
  if [[ "$RESET_DISK" == 1 ]]; then rm -f -- "$QCOW2"; fi
  if [[ ! -f "$QCOW2" ]]; then
    qemu-img convert -q -f raw -O qcow2 -o compat=1.1,cluster_size=65536 "$RAW" "$QCOW2"
  fi
  qemu-img check -q -f qcow2 "$QCOW2"
  if [[ "$START_VM" == 0 ]]; then
    printf 'QEMU appliance prepared: %s\n' "$QCOW2"
    return
  fi
  exec qemu-system-i386 \
    -machine pc,vmport=off \
    -m 64M \
    -vga std \
    -drive "file=$QCOW2,format=qcow2,if=ide,index=0,media=disk" \
    -drive "file=$ISO,format=raw,if=ide,index=2,media=cdrom,readonly=on" \
    -boot order=d,strict=on
}

prepare_virtualbox() {
  command -v VBoxManage >/dev/null 2>&1 || { echo "VBoxManage was not found" >&2; exit 1; }
  if [[ "$RESET_DISK" == 1 ]]; then rm -f -- "$VDI"; fi
  if [[ ! -f "$VDI" ]]; then
    VBoxManage convertfromraw "$RAW" "$VDI" --format VDI >/dev/null
  fi
  if VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
    echo "VirtualBox VM already exists: $VM_NAME" >&2
    echo "Use a different ZENOV_VM_NAME or remove the existing VM explicitly." >&2
    exit 1
  fi
  VBoxManage createvm --name "$VM_NAME" --ostype Other --register >/dev/null
  rollback_vm() {
    VBoxManage unregistervm "$VM_NAME" >/dev/null 2>&1 || true
  }
  trap rollback_vm ERR
  VBoxManage modifyvm "$VM_NAME" \
    --memory 64 \
    --firmware bios \
    --boot1 dvd --boot2 disk --boot3 none --boot4 none \
    --acpi on --ioapic off --nic1 none >/dev/null
  VBoxManage storagectl "$VM_NAME" \
    --name "IDE Controller" --add ide --controller PIIX4 --bootable on >/dev/null
  VBoxManage storageattach "$VM_NAME" \
    --storagectl "IDE Controller" --port 0 --device 0 --type hdd --medium "$VDI" >/dev/null
  VBoxManage storageattach "$VM_NAME" \
    --storagectl "IDE Controller" --port 1 --device 0 --type dvddrive --medium "$ISO" >/dev/null
  trap - ERR
  if [[ "$START_VM" == 1 ]]; then
    VBoxManage startvm "$VM_NAME"
  else
    printf 'VirtualBox appliance prepared: %s\n' "$VM_NAME"
  fi
}

prepare_vmware() {
  if [[ "$RESET_DISK" == 1 ]]; then rm -f -- "$VMDK"; fi
  if [[ ! -f "$VMDK" ]]; then
    if command -v vmware-vdiskmanager >/dev/null 2>&1; then
      vmware-vdiskmanager -r "$RAW" -t 0 "$VMDK" >/dev/null
    elif command -v qemu-img >/dev/null 2>&1; then
      qemu-img convert -q -f raw -O vmdk -o subformat=monolithicSparse,compat6 "$RAW" "$VMDK"
    else
      echo "Neither vmware-vdiskmanager nor qemu-img was found" >&2
      exit 1
    fi
  fi
  [[ -s "$VMX" ]] || { echo "Missing VMware configuration: $VMX" >&2; exit 1; }
  if command -v qemu-img >/dev/null 2>&1; then qemu-img check -q -f vmdk "$VMDK"; fi
  if [[ "$START_VM" == 1 ]] && command -v vmrun >/dev/null 2>&1; then
    vmrun start "$VMX" gui
  else
    printf 'VMware appliance prepared. Open: %s\n' "$VMX"
  fi
}

case "$HYPERVISOR" in
  qemu) prepare_qemu ;;
  virtualbox) prepare_virtualbox ;;
  vmware) prepare_vmware ;;
  -h|--help|help) usage ;;
  *) usage >&2; exit 2 ;;
esac
