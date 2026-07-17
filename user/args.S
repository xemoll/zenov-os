.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start
.equ SYS_EXIT, 0
.equ SYS_WRITE, 1
.equ ERROR_UNSUPPORTED, 0xFFFFFFFA

_start:
    cmp dword ptr [esp], 3
    jne .fail
    mov esi, [esp + 4]
    mov ebx, [esi + 4]
    cmp dword ptr [ebx], 0x68706c61
    jne .fail
    cmp byte ptr [ebx + 4], 'a'
    jne .fail
    cmp byte ptr [ebx + 5], 0
    jne .fail
    mov ebx, [esi + 8]
    cmp dword ptr [ebx], 0x61746562
    jne .fail
    cmp byte ptr [ebx + 4], 0
    jne .fail
    mov eax, 255
    int 0x80
    cmp eax, ERROR_UNSUPPORTED
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
success:
    .ascii "PROCESS_ARGV_OK\nSYSCALL_ERRORS_OK\n"
success_end:
failure:
    .ascii "PROCESS_ARGV_FAIL\n"
failure_end:
.section .note.GNU-stack,"",@progbits
