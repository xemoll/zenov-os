.intel_syntax noprefix
.section .rodata.process_lifecycle_probe,"a",@progbits
.code32

.global process_lifecycle_probe_start
.global process_lifecycle_probe_end
.type process_lifecycle_probe_start, @object

/*
 * Kernel-owned PIC ring-3 parent used to prove process-object lifetime.
 * It spawns the existing signed HELLO.ZEX twice, waits through both the
 * process-handle and parent/child paths, then proves a closed handle is stale.
 */
process_lifecycle_probe_start:
    call .base
.base:
    pop ebp
    sub esp, 96

    lea ebx, [ebp + .parent_start - .base]
    mov ecx, .parent_start_end - .parent_start
    call .write

    /* child #1: spawn and receive a wait/query handle */
    lea ebx, [ebp + .child_path - .base]
    xor ecx, ecx
    lea edx, [esp + 0]
    mov eax, 12
    int 0x80
    cmp eax, 0xfffffff0
    jae .failed
    mov [esp + 4], eax

    /* query the live object and verify the returned stable PID */
    mov ebx, [esp + 0]
    lea ecx, [esp + 16]
    mov eax, 16
    int 0x80
    test eax, eax
    jne .failed
    mov eax, [esp + 16]
    cmp eax, [esp + 4]
    jne .failed

    /* wait by handle; status layout is pid, exit, reason, fault. */
    mov ebx, [esp + 0]
    lea ecx, [esp + 48]
    xor edx, edx
    mov eax, 14
    int 0x80
    cmp eax, [esp + 4]
    jne .failed
    cmp dword ptr [esp + 48], eax
    jne .failed
    cmp dword ptr [esp + 52], 0
    jne .failed
    cmp dword ptr [esp + 56], 1
    jne .failed

    /* parent wait reaps the zombie, while the handle retains its object. */
    mov ebx, [esp + 4]
    lea ecx, [esp + 64]
    xor edx, edx
    mov eax, 13
    int 0x80
    cmp eax, [esp + 4]
    jne .failed

    mov ebx, [esp + 0]
    mov eax, 15
    int 0x80
    test eax, eax
    jne .failed

    lea ebx, [ebp + .child_one - .base]
    mov ecx, .child_one_end - .child_one
    call .write

    /* child #2 must receive a different generation-safe PID. */
    lea ebx, [ebp + .child_path - .base]
    xor ecx, ecx
    lea edx, [esp + 0]
    mov eax, 12
    int 0x80
    cmp eax, 0xfffffff0
    jae .failed
    mov [esp + 8], eax
    cmp eax, [esp + 4]
    je .failed

    mov ebx, eax
    lea ecx, [esp + 48]
    xor edx, edx
    mov eax, 13
    int 0x80
    cmp eax, [esp + 8]
    jne .failed
    cmp dword ptr [esp + 52], 0
    jne .failed
    cmp dword ptr [esp + 56], 1
    jne .failed

    lea ebx, [ebp + .generation - .base]
    mov ecx, .generation_end - .generation
    call .write

    mov ebx, [esp + 0]
    mov eax, 15
    int 0x80
    test eax, eax
    jne .failed

    /* A closed handle must never resolve after generation rollover. */
    mov ebx, [esp + 0]
    lea ecx, [esp + 16]
    mov eax, 16
    int 0x80
    cmp eax, 0xffffffff
    jne .failed

    lea ebx, [ebp + .stale - .base]
    mov ecx, .stale_end - .stale
    call .write
    lea ebx, [ebp + .ok - .base]
    mov ecx, .ok_end - .ok
    call .write
    xor ebx, ebx
    xor eax, eax
    int 0x80

.failed:
    lea ebx, [ebp + .fail - .base]
    mov ecx, .fail_end - .fail
    call .write
    mov ebx, 1
    xor eax, eax
    int 0x80
.hang:
    ud2
    jmp .hang

.write:
    mov eax, 1
    int 0x80
    ret

.child_path: .asciz "/apps/hello.zex"
.parent_start: .ascii "PROCESS_PARENT_START\n"
.parent_start_end:
.child_one: .ascii "PROCESS_CHILD_ONE_REAPED\n"
.child_one_end:
.generation: .ascii "PROCESS_PID_GENERATION_OK\n"
.generation_end:
.stale: .ascii "PROCESS_HANDLE_STALE_BLOCKED\n"
.stale_end:
.ok: .ascii "PROCESS_LIFECYCLE_PROBE_OK\n"
.ok_end:
.fail: .ascii "PROCESS_LIFECYCLE_PROBE_FAILED\n"
.fail_end:

process_lifecycle_probe_end:
.size process_lifecycle_probe_start, process_lifecycle_probe_end - process_lifecycle_probe_start

.section .note.GNU-stack,"",@progbits
