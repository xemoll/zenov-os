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
    hlt
    jmp .hang

.section .rodata,"a"
message:
    .ascii "HELLO_ZEX_OK\n"
    .ascii "ZenovOS native application ABI is active.\n"
message_end:
