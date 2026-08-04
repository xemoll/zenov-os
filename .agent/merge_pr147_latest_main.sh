#!/usr/bin/env bash
set -euo pipefail

EXPECTED_SCHEDULER='2755d438ce41e0c3d430a02e7ab8e1c00543f684'
EXPECTED_MAIN='4ebb1e23400475faa691d2f619a9adf8c2f27a4b'

test "$(git rev-parse HEAD)" = "$EXPECTED_SCHEDULER"
test -z "$(git status --porcelain)"
git config user.name 'ZenovOS Kernel Agent'
git config user.email 'actions@users.noreply.github.com'
git fetch origin main
test "$(git rev-parse origin/main)" = "$EXPECTED_MAIN"
scheduler_head="$(git rev-parse HEAD)"

set +e
git merge --no-ff --no-commit "$EXPECTED_MAIN"
merge_rc=$?
set -e
test "$merge_rc" -ne 0
mapfile -t conflicts < <(git diff --name-only --diff-filter=U)
test "${#conflicts[@]}" -eq 5
printf '%s\n' "${conflicts[@]}" | sort > /tmp/conflicts.actual
cat > /tmp/conflicts.expected <<'EOF'
kernel/entry.S
kernel/kernel.cpp
kernel/parts/process.inc
kernel/parts/supervisor_layout.inc
tests/qemu_ata_read_faults.sh
EOF
diff -u /tmp/conflicts.expected /tmp/conflicts.actual

# High native user window plus dynamic Linux/i386 GDT labels.
git checkout --ours kernel/entry.S
python3 - <<'PY'
from pathlib import Path
p = Path('kernel/entry.S')
s = p.read_text()
pairs = [
('    # Ring-3 code: base 0x40000000, byte-granular 1 MiB limit, 32-bit.\n    .word 0xffff',
 '    # Ring-3 code: base 0x40000000, byte-granular 1 MiB limit, 32-bit.\n.global gdt_user_code\ngdt_user_code:\n    .word 0xffff'),
('    # Ring-3 data: same isolated 1 MiB linear window.\n    .word 0xffff',
 '    # Ring-3 data: same isolated 1 MiB linear window.\n.global gdt_user_data\ngdt_user_data:\n    .word 0xffff')]
for old, new in pairs:
    if s.count(old) != 1: raise SystemExit(f'entry anchor mismatch: {old!r}')
    s = s.replace(old, new, 1)
p.write_text(s)
PY
git add kernel/entry.S

# Latest main include graph plus scheduler dispatch.
git checkout --theirs kernel/kernel.cpp
python3 - <<'PY'
from pathlib import Path
p = Path('kernel/kernel.cpp')
s = p.read_text()
old = '#include "parts/process_linux_i386.inc"\n#include "parts/graphics.inc"'
new = '#include "parts/process_linux_i386.inc"\n#include "parts/scheduler.inc"\n#include "parts/graphics.inc"'
if s.count(old) != 1: raise SystemExit('kernel include anchor mismatch')
p.write_text(s.replace(old, new, 1))
PY
git add kernel/kernel.cpp

# Scheduler process ABI plus dynamic user segment rebasing.
git checkout --ours kernel/parts/process.inc
python3 - <<'PY'
from pathlib import Path
p = Path('kernel/parts/process.inc')
s = p.read_text()
old = 'extern "C" uint8_t gdt_tss[];\n'
new = old + 'extern "C" uint8_t gdt_user_code[];\nextern "C" uint8_t gdt_user_data[];\n'
if s.count(old) != 1: raise SystemExit('GDT extern anchor mismatch')
s = s.replace(old, new, 1)
anchor = 'bool user_range(uint32_t offset, uint32_t length, bool write = false)'
functions = '''void descriptor_set_user(uint8_t descriptor[8], uintptr_t base, uint32_t limit, uint8_t access) {
    uint32_t encoded_limit = limit;
    uint8_t flags = 0x40U;
    if (limit > 0x000FFFFFU) { encoded_limit = limit >> 12U; flags = 0xC0U; }
    descriptor[0] = static_cast<uint8_t>(encoded_limit);
    descriptor[1] = static_cast<uint8_t>(encoded_limit >> 8U);
    descriptor[2] = static_cast<uint8_t>(base);
    descriptor[3] = static_cast<uint8_t>(base >> 8U);
    descriptor[4] = static_cast<uint8_t>(base >> 16U);
    descriptor[5] = access;
    descriptor[6] = static_cast<uint8_t>(flags | ((encoded_limit >> 16U) & 0x0FU));
    descriptor[7] = static_cast<uint8_t>(base >> 24U);
}
bool configure_user_segments(uint32_t logical_bias) {
    if (logical_bias > 0xFFFFFFFFU - user_limit || logical_bias > user_base) return false;
    const uintptr_t base = user_base - logical_bias;
    const uint32_t limit = logical_bias + user_limit - 1U;
    descriptor_set_user(gdt_user_code, base, limit, 0xFAU);
    descriptor_set_user(gdt_user_data, base, limit, 0xF2U);
    return true;
}
void restore_native_user_segments() {
    descriptor_set_user(gdt_user_code, user_base, user_limit - 1U, 0xFAU);
    descriptor_set_user(gdt_user_data, user_base, user_limit - 1U, 0xF2U);
}
'''
if s.count(anchor) != 1: raise SystemExit('user range anchor mismatch')
s = s.replace(anchor, functions + anchor, 1)
old_init = 'void init() {\n    memset(&kernel_tss, 0, sizeof(kernel_tss));'
new_init = 'void init() {\n    restore_native_user_segments();\n    memset(&kernel_tss, 0, sizeof(kernel_tss));'
if s.count(old_init) != 1: raise SystemExit('process init anchor mismatch')
p.write_text(s.replace(old_init, new_init, 1))
PY
git add kernel/parts/process.inc

# Remove a duplicate profile declaration only if auto-merge retained it.
python3 - <<'PY'
from pathlib import Path
import re
p = Path('kernel/parts/process_capabilities.inc')
s = p.read_text()
pattern = r'struct ActiveCapabilityProfile \{.*?\};\n\n'
updated, count = re.subn(pattern, '', s, count=1, flags=re.S)
if count not in (0, 1): raise SystemExit('unexpected capability profile count')
p.write_text(updated)
PY
git add kernel/parts/process_capabilities.inc

# Native scheduled tasks must never inherit foreground Linux ABI state.
python3 - <<'PY'
from pathlib import Path
p = Path('kernel/parts/scheduler.inc')
s = p.read_text()
old = '    active_capabilities = task.capabilities;\n'
new = old + '    active_syscall_abi = SyscallAbi::zenov;\n    linux_i386_image_bias = 0U;\n'
if s.count(old) != 1: raise SystemExit('scheduler exposure anchor mismatch')
p.write_text(s.replace(old, new, 1))
PY
git add kernel/parts/scheduler.inc

cat > kernel/parts/supervisor_layout.inc <<'EOF'
namespace supervisor_layout {
constexpr uintptr_t heap_start = 0x00100000U;
constexpr uintptr_t heap_end = 0x00300000U;
constexpr uintptr_t scan_workspace = 0x00300000U;
constexpr uint32_t scan_workspace_bytes = 0x00010000U;
constexpr uintptr_t zvrt_records = 0x00310000U;
constexpr uint32_t zvrt_records_bytes = 0x00001000U;
constexpr uintptr_t pmm_bitmap = 0x00311000U;
constexpr uint32_t pmm_bitmap_bytes = 0x00001000U;
constexpr uintptr_t policy_transaction = 0x00312000U;
constexpr uint32_t policy_transaction_bytes = 0x00004000U;
constexpr uintptr_t live_image_workspace = 0x00316000U;
constexpr uint32_t live_image_workspace_bytes = 0x00010000U;
constexpr uintptr_t live_overlay_map = 0x00326000U;
constexpr uint32_t live_overlay_map_bytes = 0x00010000U;
constexpr uintptr_t live_overlay_data = 0x00336000U;
constexpr uint32_t live_overlay_data_bytes = 0x00080000U;
constexpr uintptr_t live_storage_end = live_overlay_data + live_overlay_data_bytes;
constexpr uintptr_t kernel_static_start = live_storage_end;
constexpr uintptr_t kernel_static_limit = 0x00480000U;
constexpr uint32_t task_stack_count = 8U;
constexpr uint32_t task_stack_guard_bytes = 0x00001000U;
constexpr uint32_t task_stack_bytes = 0x00004000U;
constexpr uint32_t task_stack_stride = task_stack_guard_bytes + task_stack_bytes;
constexpr uintptr_t task_stack_region_start = 0x00500000U;
constexpr uintptr_t task_stack_region_end = task_stack_region_start + task_stack_count * task_stack_stride;
constexpr uintptr_t legacy_user_physical_start = 0x00600000U;
constexpr uintptr_t legacy_user_physical_end = 0x00700000U;
constexpr uintptr_t user_physical_end = legacy_user_physical_end;
constexpr uintptr_t user_base = 0x40000000U;
constexpr uint32_t user_window_bytes = 0x00100000U;
constexpr uintptr_t zmid_workspace_physical_a = 0x00800000U;
constexpr uintptr_t zmid_workspace_physical_b = 0x00810000U;
constexpr uintptr_t zmid_workspace_virtual_a = 0x00800000U;
constexpr uintptr_t zmid_workspace_virtual_b = 0x00810000U;
constexpr uint32_t zmid_workspace_bytes = 0x00010000U;
constexpr uintptr_t zmid_workspace_virtual_end = 0x00820000U;
constexpr uintptr_t pmm_reserved_end = 0x00820000U;
constexpr uintptr_t kernel_identity_hole_start = 0x00800000U;
constexpr uintptr_t kernel_identity_hole_end = 0x00C00000U;
static_assert(heap_start < heap_end);
static_assert(heap_end == scan_workspace);
static_assert(scan_workspace + scan_workspace_bytes == zvrt_records);
static_assert(zvrt_records + zvrt_records_bytes == pmm_bitmap);
static_assert(pmm_bitmap + pmm_bitmap_bytes == policy_transaction);
static_assert(policy_transaction + policy_transaction_bytes == live_image_workspace);
static_assert(live_image_workspace + live_image_workspace_bytes == live_overlay_map);
static_assert(live_overlay_map + live_overlay_map_bytes == live_overlay_data);
static_assert(live_storage_end == kernel_static_start);
static_assert(kernel_static_start < kernel_static_limit);
static_assert(kernel_static_limit <= task_stack_region_start);
static_assert(task_stack_region_end <= legacy_user_physical_start);
static_assert(legacy_user_physical_start < legacy_user_physical_end);
static_assert(user_physical_end <= zmid_workspace_physical_a);
static_assert(zmid_workspace_physical_a + zmid_workspace_bytes == zmid_workspace_physical_b);
static_assert(zmid_workspace_physical_b + zmid_workspace_bytes == pmm_reserved_end);
static_assert(zmid_workspace_virtual_a == kernel_identity_hole_start);
static_assert(zmid_workspace_virtual_a + zmid_workspace_bytes == zmid_workspace_virtual_b);
static_assert(zmid_workspace_virtual_b + zmid_workspace_bytes == zmid_workspace_virtual_end);
static_assert(zmid_workspace_virtual_end <= kernel_identity_hole_end);
static_assert(kernel_identity_hole_end - kernel_identity_hole_start == 0x00400000U);
} // namespace supervisor_layout
EOF
git add kernel/parts/supervisor_layout.inc

python3 - <<'PY'
from pathlib import Path
p = Path('kernel/parts/memory.inc')
s = p.read_text()
pairs = [
('constexpr uintptr_t reserved_end = supervisor_layout::pmm_reserved_end;\nconstexpr uint32_t self_test_frames = 16U;',
 'constexpr uintptr_t reserved_end = supervisor_layout::pmm_reserved_end;\nconstexpr uintptr_t identity_hole_start = supervisor_layout::kernel_identity_hole_start;\nconstexpr uintptr_t identity_hole_end = supervisor_layout::kernel_identity_hole_end;\nconstexpr uint32_t self_test_frames = 16U;'),
('    for (uint32_t address = start; address < end; address += page_size) {\n        const uint32_t frame = address / page_size;',
 '    for (uint32_t address = start; address < end; address += page_size) {\n        if (address >= identity_hole_start && address < identity_hole_end) continue;\n        const uint32_t frame = address / page_size;'),
('bool free_frame(uintptr_t address) {\n    if (!initialized || address < reserved_end || address >= managed_limit || (address & (page_size - 1U))) return false;',
 'bool free_frame(uintptr_t address) {\n    if (!initialized || address < reserved_end || address >= managed_limit ||\n        (address >= identity_hole_start && address < identity_hole_end) ||\n        (address & (page_size - 1U))) return false;'),
('constexpr uint32_t kernel_table_count = pmm::managed_limit / table_span;\nconstexpr uint32_t user_virtual_base = 0x40000000U;',
 'constexpr uint32_t kernel_table_count = pmm::managed_limit / table_span;\nconstexpr uint32_t supervisor_hole_directory_index = supervisor_layout::kernel_identity_hole_start / table_span;\nconstexpr uint32_t user_virtual_base = 0x40000000U;'),
('static_assert(kernel_table_count == 32U);\nstatic_assert(user_directory_index >= kernel_table_count && user_directory_index < 1024U);',
 'static_assert(kernel_table_count == 32U);\nstatic_assert(supervisor_layout::kernel_identity_hole_start % table_span == 0U);\nstatic_assert(supervisor_layout::kernel_identity_hole_end - supervisor_layout::kernel_identity_hole_start == table_span);\nstatic_assert(supervisor_hole_directory_index == 2U);\nstatic_assert(user_directory_index >= kernel_table_count && user_directory_index < 1024U);'),
('        page_directory[table] = reinterpret_cast<uintptr_t>(kernel_tables[table]) | present | writable;',
 '        if (table != supervisor_hole_directory_index) {\n            page_directory[table] = reinterpret_cast<uintptr_t>(kernel_tables[table]) | present | writable;\n        }'),
('    serial::line("KERNEL_IDENTITY_MAP_OK bytes=134217728 supervisor-only=yes");',
 '    serial::line("KERNEL_IDENTITY_MAP_OK bytes=134217728 supervisor-only=yes excluded=0x00800000-0x00c00000");\n    serial::line("PMM_IDENTITY_HOLE_RESERVED_OK bytes=4194304 reason=zmid-supervisor-pde");')]
for old, new in pairs:
    if s.count(old) != 1: raise SystemExit(f'memory anchor count={s.count(old)}: {old[:60]!r}')
    s = s.replace(old, new, 1)
p.write_text(s)
PY
git add kernel/parts/memory.inc

git checkout --ours tests/qemu_ata_read_faults.sh
git add tests/qemu_ata_read_faults.sh

test -z "$(git diff --name-only --diff-filter=U)"
git diff --cached --check
grep -Fq 'supervisor_hole_directory_index == 2U' kernel/parts/memory.inc
grep -Fq 'active_syscall_abi = SyscallAbi::zenov' kernel/parts/scheduler.inc
grep -Fq '#include "parts/process_linux_i386.inc"' kernel/kernel.cpp
grep -Fq '#include "parts/scheduler.inc"' kernel/kernel.cpp
grep -Fq 'Storage: attached ZenovFS mount failed status=io-error' tests/qemu_ata_read_faults.sh

git restore --source="$scheduler_head" --staged --worktree -- .github/workflows

sudo apt-get update
sudo apt-get install -y build-essential binutils clang qemu-system-x86
make clean
make -j2 all check | tee combined-host-check.log
grep -Fq 'SCHEDULER_POLICY_TEST_OK priority-levels=4 rr=equal aging=50 quantum=3-6' combined-host-check.log
grep -Fq 'LINUX_I386_ABI_TEST_OK' combined-host-check.log
grep -Fq 'PACKAGE_COMPATIBILITY_PREFLIGHT_TEST_OK' combined-host-check.log
echo '2b7ba0114d5228825b30aca30e0e978f2faf9b798cf7f5494742d7a1d330956a  build/HELLO.ZEX' | sha256sum -c -
static_end="$(awk '/__kernel_static_end = \./ {print $1}' build/kernel.map | tail -1)"
[[ "$static_end" =~ ^0x[0-9a-fA-F]+$ ]]
test $((static_end)) -le $((0x480000))

bash tests/qemu_scheduler.sh build/zenov-os.img build/zenov-data.img build/qemu/scheduler-sync
bash tests/qemu_zenpkg_foreign.sh build/zenov-os.img build/zenov-data.img build/qemu/foreign-sync
grep -Fq 'PREEMPTIVE_MULTITASKING_OK isolation=per-address-space' build/qemu/scheduler-sync/serial.log
grep -Fq 'LINUX_I386_COMPAT_EXIT code=37' build/qemu/foreign-sync/serial.log
! grep -Fq 'ZENOVOS KERNEL PANIC' build/qemu/scheduler-sync/serial.log
! grep -Fq 'ZENOVOS KERNEL PANIC' build/qemu/foreign-sync/serial.log

git add -A
git reset -- .github/workflows
test -z "$(git diff --cached --name-only -- .github/workflows)"
git diff --cached --check
git commit -m 'merge: integrate scheduler with security and Linux compatibility main'
test "$(git rev-parse HEAD^1)" = "$EXPECTED_SCHEDULER"
test "$(git rev-parse HEAD^2)" = "$EXPECTED_MAIN"
git push origin HEAD:refs/heads/agent/kernel-preemptive-scheduler-0.1.1
printf 'PR147_MAIN_SYNC_OK head=%s tree=%s main=%s\n' "$(git rev-parse HEAD)" "$(git rev-parse HEAD^{tree})" "$EXPECTED_MAIN"
