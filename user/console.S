.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start
.equ SYS_EXIT, 0
.equ SYS_WRITE, 1
.equ SYS_READ, 8

_start:
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:prompt
    mov ecx, prompt_end - prompt
    int 0x80
    mov eax, SYS_READ
    mov ebx, OFFSET FLAT:buffer
    mov ecx, 32
    int 0x80
    cmp eax, 5
    jne .fail
    cmp dword ptr [buffer], 0x6f6e657a
    jne .fail
    cmp byte ptr [buffer + 4], 'v'
    jne .fail
    cmp byte ptr [buffer + 5], 0
    jne .fail
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:success
    mov ecx, success_end - success
    int 0x80
    xor ebx, ebx
    mov eax, SYS_EXIT
    int 0x80
.fail:
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:failure
    mov ecx, failure_end - failure
    int 0x80
    mov ebx, 1
    mov eax, SYS_EXIT
    int 0x80

.section .rodata,"a"
prompt:
    .ascii "Console input test: type zenov and press Enter.\n"
prompt_end:
success:
    .ascii "CONSOLE_READ_SYSCALL_OK\n"
success_end:
failure:
    .ascii "CONSOLE_READ_SYSCALL_FAIL\n"
failure_end:
.section .bss,"aw",@nobits
.align 16
buffer:
    .skip 32
.section .note.GNU-stack,"",@progbits
