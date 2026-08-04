.intel_syntax noprefix
.section .rodata.scheduler_probe,"a",@progbits
.code32

.global scheduler_probe_start
.global scheduler_probe_end
.type scheduler_probe_start, @object

/*
 * Position-independent ring-3 workload used only by the kernel scheduler
 * diagnostic.  EAX selects workload A (0) or B (non-zero).  The complete
 * image is copied from kernel read-only storage into a fresh per-task address
 * space, so the signed ZenovFS application trust set remains unchanged.
 */
scheduler_probe_start:
    mov ebp, eax
    call .base
.base:
    pop esi
    test ebp, ebp
    jne .mode_b

.mode_a:
    lea ebx, [esi + .a_start - .base]
    mov ecx, .a_start_end - .a_start
    call .write
    call .burn
    lea ebx, [esi + .a_done - .base]
    mov ecx, .a_done_end - .a_done
    call .write
    jmp .exit

.mode_b:
    lea ebx, [esi + .b_start - .base]
    mov ecx, .b_start_end - .b_start
    call .write
    call .burn
    lea ebx, [esi + .b_done - .base]
    mov ecx, .b_done_end - .b_done
    call .write

.exit:
    xor ebx, ebx
    xor eax, eax
    int 0x80
.hang:
    ud2
    jmp .hang

.write:
    mov eax, 1
    int 0x80
    ret

/* Long enough to cross multiple 100 Hz quanta under QEMU TCG, but bounded. */
.burn:
    push ecx
    push edx
    mov edx, 16
.outer:
    mov ecx, 0x00200000
.inner:
    dec ecx
    jnz .inner
    dec edx
    jnz .outer
    pop edx
    pop ecx
    ret

.a_start: .ascii "PREEMPT_A_START\n"
.a_start_end:
.a_done: .ascii "PREEMPT_A_DONE\n"
.a_done_end:
.b_start: .ascii "PREEMPT_B_START\n"
.b_start_end:
.b_done: .ascii "PREEMPT_B_DONE\n"
.b_done_end:

scheduler_probe_end:
.size scheduler_probe_start, scheduler_probe_end - scheduler_probe_start

.section .note.GNU-stack,"",@progbits
