.intel_syntax noprefix
.include "user/sdk/syscalls.inc"
.section .text,"ax"
.code32
.global _start

_start:
    mov eax, ZENOV_SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:start_message
    mov ecx, start_message_end - start_message
    int 0x80

    # UD2 must raise #UD in ring 3. ZenovOS should terminate this application,
    # restore the saved kernel stack and continue the system console.
    ud2

    mov ebx, 99
    mov eax, ZENOV_SYS_EXIT
    int 0x80

.section .rodata,"a"
start_message:
    .ascii "FAULT_ELF_TRIGGER\n"
start_message_end:
