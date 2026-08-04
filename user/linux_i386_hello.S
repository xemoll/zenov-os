.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start

_start:
    # Unknown Linux syscalls must fail closed with -ENOSYS.
    mov eax, 5
    int 0x80
    cmp eax, -38
    jne .enosys_failed

    # write() is restricted to the console descriptors.
    mov eax, 4
    mov ebx, 3
    mov ecx, OFFSET FLAT:message
    mov edx, message_end - message
    int 0x80
    cmp eax, -9
    jne .fd_failed

    mov eax, 4
    mov ebx, 1
    mov ecx, OFFSET FLAT:message
    mov edx, message_end - message
    int 0x80
    cmp eax, message_end - message
    jne .write_failed

    mov eax, 252
    xor ebx, ebx
    int 0x80

.enosys_failed:
    mov ebx, 41
    jmp .exit
.fd_failed:
    mov ebx, 42
    jmp .exit
.write_failed:
    mov ebx, 43
.exit:
    mov eax, 1
    int 0x80
    ud2

.section .rodata,"a"
message:
    .ascii "LINUX_I386_COMPAT_WRITE_OK\n"
message_end:

.section .note.GNU-stack,"",@progbits
