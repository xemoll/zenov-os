.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start
.equ SYS_WRITE, 1

_start:
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:message
    mov ecx, message_end - message
    int 0x80
    mov ax, 0x10
    mov ds, ax
    ud2

.section .rodata,"a"
message:
    .ascii "Attempting to load a supervisor-only data selector.\n"
message_end:
.section .note.GNU-stack,"",@progbits
