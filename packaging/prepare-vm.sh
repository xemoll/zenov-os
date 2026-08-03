#!/usr/bin/env bash
set -euo pipefail

VERSION="0.1.1"
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HYPERVISOR="${1:-qemu}"
VM_NAME="${ZENOV_VM_NAME:-ZenovOS $VERSION}"
START_VM="${ZENOV_VM_START:-1}"
RESET_DISK="${ZENOV_VM_RESET:-0}"
REPAIR_VM="${ZENOV_VM_REPAIR:-1}"
SOFTWARE_FALLBACK="${ZENOV_VM_SOFTWARE_FALLBACK:-1}"
QEMU_ACCEL="${ZENOV_QEMU_ACCEL:-auto}"
ISO="$BASE_DIR/ZenovOS-$VERSION-x86.iso"
RAW="$BASE_DIR/ZenovOS-$VERSION-data.img"
QCOW2="$BASE_DIR/ZenovOS-$VERSION-data.qcow2"
VDI="$BASE_DIR/ZenovOS-$VERSION-data.vdi"
VMDK="$BASE_DIR/ZenovOS-$VERSION-data.vmdk"
VMX="$BASE_DIR/ZenovOS-$VERSION.vmx"
CHECKSUMS="$BASE_DIR/SHA256SUMS.txt"

usage() {
  cat <<EOF_USAGE
Usage: $(basename "$0") qemu|virtualbox|vmware

Environment:
  ZENOV_VM_NAME                 VirtualBox display name (default: $VM_NAME)
  ZENOV_VM_START                1 to start after preparation, 0 to prepare only
  ZENOV_VM_RESET                1 to recreate the selected writable disk from data.img
  ZENOV_VM_REPAIR               1 to repair/reuse an existing VirtualBox VM (default: 1)
  ZENOV_VM_SOFTWARE_FALLBACK    1 to use QEMU TCG when VirtualBox cannot access AMD-V/VT-x
  ZENOV_QEMU_ACCEL              auto, kvm or tcg (default: auto)

The ISO is read-only. Persistent files, settings and packages are stored in the
selected writable data disk. Keep a backup before setting ZENOV_VM_RESET=1.

VirtualBox cannot enable AMD-V/VT-x from inside a guest image. When VirtualBox
returns VERR_SVM_DISABLED, VERR_SVM_IN_USE or VERR_VMX_IN_VMX_ROOT_MODE, this
helper can start the same ISO and VDI through QEMU software emulation instead.
EOF_USAGE
}

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

validate_bool() {
  local name="$1" value="$2"
  case "$value" in 0|1) ;; *) fail "$name must be 0 or 1" ;; esac
}

verify_immutable_seeds() {
  local selected count
  [[ -f "$CHECKSUMS" ]] || return 0
  selected="$(grep -E "  (ZenovOS-$VERSION-x86\\.iso|ZenovOS-$VERSION-data\\.img)$" "$CHECKSUMS" || true)"
  count="$(printf '%s\n' "$selected" | sed '/^$/d' | wc -l | tr -d ' ')"
  [[ "$count" -eq 2 ]] || fail "Checksum file does not contain exactly the ISO and canonical data seed"
  if command -v sha256sum >/dev/null 2>&1; then
    (cd "$BASE_DIR" && printf '%s\n' "$selected" | sha256sum -c -)
  elif command -v shasum >/dev/null 2>&1; then
    (cd "$BASE_DIR" && printf '%s\n' "$selected" | shasum -a 256 -c -)
  else
    fail "SHA-256 verification requires sha256sum or shasum"
  fi
}

QEMU_ACCEL_ARGS=()
select_qemu_accel() {
  case "$QEMU_ACCEL" in
    tcg)
      QEMU_ACCEL_ARGS=(-accel 'tcg,thread=multi')
      ;;
    kvm)
      [[ -r /dev/kvm && -w /dev/kvm ]] || fail "ZENOV_QEMU_ACCEL=kvm requested, but /dev/kvm is unavailable"
      QEMU_ACCEL_ARGS=(-accel kvm)
      ;;
    auto)
      if [[ -r /dev/kvm && -w /dev/kvm ]] && qemu-system-i386 -accel help 2>/dev/null | grep -qx kvm; then
        QEMU_ACCEL_ARGS=(-accel kvm)
      else
        QEMU_ACCEL_ARGS=(-accel 'tcg,thread=multi')
      fi
      ;;
    *) fail "ZENOV_QEMU_ACCEL must be auto, kvm or tcg" ;;
  esac
}

run_qemu_image() {
  local disk="$1" format="$2" reason="${3:-requested}"
  command -v qemu-system-i386 >/dev/null 2>&1 || {
    printf 'VirtualBox cannot start this VM (%s).\n' "$reason" >&2
    printf 'Install qemu-system-x86 and run this helper again, or enable SVM/AMD-V in UEFI/BIOS.\n' >&2
    return 1
  }
  select_qemu_accel
  printf 'Starting ZenovOS with QEMU software-compatible path (%s, disk=%s).\n' "$reason" "$format" >&2
  exec qemu-system-i386 \
    "${QEMU_ACCEL_ARGS[@]}" \
    -machine pc,vmport=off \
    -m 64M \
    -smp 1 \
    -vga std \
    -nic none \
    -drive "file=$disk,format=$format,if=ide,index=0,media=disk" \
    -drive "file=$ISO,format=raw,if=ide,index=2,media=cdrom,readonly=on" \
    -boot order=d,strict=on
}

virtualbox_state() {
  VBoxManage showvminfo "$VM_NAME" --machinereadable 2>/dev/null \
    | sed -n 's/^VMState="\([^"]*\)"$/\1/p' \
    | head -n1
}

virtualbox_error_is_hwvirt() {
  grep -Eq 'VERR_(SVM_DISABLED|SVM_IN_USE|VMX_NO_VMX|VMX_IN_VMX_ROOT_MODE|NEM_NOT_AVAILABLE|SUPDRV_NO_RAW_MODE_HYPER_V_ROOT)'
}

repair_virtualbox_vm() {
  local info state
  info="$(VBoxManage showvminfo "$VM_NAME" --machinereadable)"
  state="$(virtualbox_state)"
  case "$state" in
    poweroff|aborted|saved|'') ;;
    *) fail "VirtualBox VM '$VM_NAME' must be fully powered off before repair (state: $state)" ;;
  esac
  if [[ "$state" == saved ]]; then
    VBoxManage discardstate "$VM_NAME" >/dev/null
  fi

  VBoxManage modifyvm "$VM_NAME" \
    --ostype Other \
    --memory 64 \
    --cpus 1 \
    --firmware bios \
    --boot1 dvd --boot2 disk --boot3 none --boot4 none \
    --acpi on --ioapic off --nic1 none \
    --paravirtprovider none \
    --nested-hw-virt off >/dev/null

  # Older VirtualBox releases can use a 32-bit software execution path. Newer
  # releases may reject or ignore --hwvirtex off; the QEMU TCG fallback below
  # remains authoritative when hardware virtualization is unavailable.
  if VBoxManage modifyvm "$VM_NAME" --hwvirtex off --nestedpaging off >/dev/null 2>&1; then
    :
  fi

  info="$(VBoxManage showvminfo "$VM_NAME" --machinereadable)"
  if ! grep -Fq "$VDI" <<<"$info"; then
    if ! grep -Eq '^storagecontrollername[0-9]+="IDE Controller"$' <<<"$info"; then
      VBoxManage storagectl "$VM_NAME" \
        --name "IDE Controller" --add ide --controller PIIX4 --bootable on >/dev/null
    fi
    VBoxManage storageattach "$VM_NAME" \
      --storagectl "IDE Controller" --port 0 --device 0 --type hdd --medium "$VDI" >/dev/null
  fi

  info="$(VBoxManage showvminfo "$VM_NAME" --machinereadable)"
  if ! grep -Fq "$ISO" <<<"$info"; then
    VBoxManage storageattach "$VM_NAME" \
      --storagectl "IDE Controller" --port 1 --device 0 --type dvddrive --medium "$ISO" >/dev/null
  fi
}

validate_bool ZENOV_VM_START "$START_VM"
validate_bool ZENOV_VM_RESET "$RESET_DISK"
validate_bool ZENOV_VM_REPAIR "$REPAIR_VM"
validate_bool ZENOV_VM_SOFTWARE_FALLBACK "$SOFTWARE_FALLBACK"
case "$QEMU_ACCEL" in auto|kvm|tcg) ;; *) fail "ZENOV_QEMU_ACCEL must be auto, kvm or tcg" ;; esac
[[ -s "$ISO" ]] || fail "Missing boot ISO: $ISO"
[[ -s "$RAW" ]] || fail "Missing canonical data image: $RAW"
verify_immutable_seeds

prepare_qemu() {
  command -v qemu-img >/dev/null 2>&1 || fail "qemu-img was not found"
  command -v qemu-system-i386 >/dev/null 2>&1 || fail "qemu-system-i386 was not found"
  if [[ "$RESET_DISK" == 1 ]]; then rm -f -- "$QCOW2"; fi
  if [[ ! -f "$QCOW2" ]]; then
    qemu-img convert -q -f raw -O qcow2 -o compat=1.1,cluster_size=65536 "$RAW" "$QCOW2"
  fi
  qemu-img check -q -f qcow2 "$QCOW2"
  if [[ "$START_VM" == 0 ]]; then
    printf 'QEMU appliance prepared: %s\n' "$QCOW2"
    return
  fi
  run_qemu_image "$QCOW2" qcow2 "QEMU launch"
}

prepare_virtualbox() {
  local created=0 start_output
  command -v VBoxManage >/dev/null 2>&1 || fail "VBoxManage was not found"
  if [[ "$RESET_DISK" == 1 ]]; then rm -f -- "$VDI"; fi
  if [[ ! -f "$VDI" ]]; then
    VBoxManage convertfromraw "$RAW" "$VDI" --format VDI >/dev/null
  fi

  if VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
    [[ "$REPAIR_VM" == 1 ]] || fail "VirtualBox VM already exists: $VM_NAME"
    printf 'Repairing existing VirtualBox VM: %s\n' "$VM_NAME"
  else
    VBoxManage createvm --name "$VM_NAME" --ostype Other --register >/dev/null
    created=1
  fi

  rollback_vm() {
    if [[ "$created" == 1 ]]; then
      VBoxManage unregistervm "$VM_NAME" >/dev/null 2>&1 || true
    fi
  }
  trap rollback_vm ERR
  repair_virtualbox_vm
  trap - ERR

  if [[ "$START_VM" == 0 ]]; then
    printf 'VirtualBox appliance prepared: %s\n' "$VM_NAME"
    return
  fi

  if start_output="$(VBoxManage startvm "$VM_NAME" 2>&1)"; then
    printf '%s\n' "$start_output"
    return
  fi

  printf '%s\n' "$start_output" >&2
  if [[ "$SOFTWARE_FALLBACK" == 1 ]] && virtualbox_error_is_hwvirt <<<"$start_output"; then
    run_qemu_image "$VDI" vdi "VirtualBox hardware virtualization unavailable"
  fi
  return 1
}

prepare_vmware() {
  if [[ "$RESET_DISK" == 1 ]]; then rm -f -- "$VMDK"; fi
  if [[ ! -f "$VMDK" ]]; then
    if command -v vmware-vdiskmanager >/dev/null 2>&1; then
      vmware-vdiskmanager -r "$RAW" -t 0 "$VMDK" >/dev/null
    elif command -v qemu-img >/dev/null 2>&1; then
      qemu-img convert -q -f raw -O vmdk -o subformat=monolithicSparse,compat6 "$RAW" "$VMDK"
    else
      fail "Neither vmware-vdiskmanager nor qemu-img was found"
    fi
  fi
  [[ -s "$VMX" ]] || fail "Missing VMware configuration: $VMX"
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
