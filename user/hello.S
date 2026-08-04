.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start
.equ SYS_EXIT, 0
.equ SYS_WRITE, 1
.equ SYS_YIELD, 9
.equ SYS_GETPID, 11

_start:
    cld
    cmp dword ptr [esp], 2
    jb .default
    mov edi, [esp + 4]
    mov esi, [edi + 4]
    mov edi, OFFSET FLAT:mode_a
    mov ecx, mode_a_end - mode_a
    repe cmpsb
    je .preempt_a
    mov edi, OFFSET FLAT:mode_b
    mov ecx, mode_b_end - mode_b
    mov esi, [esp + 4]
    mov esi, [esi + 4]
    repe cmpsb
    je .preempt_b

.default:
    mov ebx, OFFSET FLAT:message
    mov ecx, message_end - message
    call .write
    xor ebx, ebx
    jmp .exit

.preempt_a:
    mov ebx, OFFSET FLAT:a_start
    mov ecx, a_start_end - a_start
    call .write
    mov eax, SYS_GETPID
    int 0x80
    call .cpu_burn
    mov ebx, OFFSET FLAT:a_done
    mov ecx, a_done_end - a_done
    call .write
    xor ebx, ebx
    jmp .exit

.preempt_b:
    mov ebx, OFFSET FLAT:b_start
    mov ecx, b_start_end - b_start
    call .write
    mov eax, SYS_YIELD
    int 0x80
    call .cpu_burn
    mov ebx, OFFSET FLAT:b_done
    mov ecx, b_done_end - b_done
    call .write
    xor ebx, ebx

.exit:
    mov eax, SYS_EXIT
    int 0x80
.hang:
    ud2
    jmp .hang

.write:
    mov eax, SYS_WRITE
    int 0x80
    ret

.cpu_burn:
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

.section .rodata,"a"
mode_a: .asciz "preempt-a"
mode_a_end:
mode_b: .asciz "preempt-b"
mode_b_end:
message:
    .ascii "HELLO_ZEX_0_1_1_OK\n"
    .ascii "ZenovOS native ZEX1 application returned through INT 0x80.\n"
message_end:
a_start: .ascii "PREEMPT_A_START\n"
a_start_end:
a_done: .ascii "PREEMPT_A_DONE\n"
a_done_end:
b_start: .ascii "PREEMPT_B_START\n"
b_start_end:
b_done: .ascii "PREEMPT_B_DONE\n"
b_done_end:

.section .note.GNU-stack,"",@progbits
