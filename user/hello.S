.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start

_start:
    mov eax, 1
    mov ebx, OFFSET FLAT:message
    mov ecx, message_end - message
    int 0x80

    xor ebx, ebx
    xor eax, eax
    int 0x80

.hang:
    ud2
    jmp .hang

.section .rodata,"a"
message:
    .ascii "HELLO_ZEX_0_1_1_OK\n"
    .ascii "ZenovOS native ZEX1 application returned through INT 0x80.\n"
message_end:
