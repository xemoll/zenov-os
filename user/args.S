.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start
.equ SYS_EXIT, 0
.equ SYS_WRITE, 1
.equ SYS_FILE_READ, 3
.equ ERROR_FAULT, 0xFFFFFFFB
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
    mov ebx, 0x8000
    mov ecx, 1
    int 0x80
    cmp eax, ERROR_FAULT
    jne .fail

    mov eax, SYS_FILE_READ
    mov ebx, OFFSET FLAT:known_file
    xor ecx, ecx
    mov edx, 1
    int 0x80
    cmp eax, ERROR_FAULT
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
known_file:
    .asciz "/data/docs/readme.txt"
success:
    .ascii "PROCESS_ARGV_OK\nSYSCALL_ERRORS_OK\nSYSCALL_POINTER_GUARD_OK\n"
success_end:
failure:
    .ascii "PROCESS_ARGV_FAIL\n"
failure_end:
.section .note.GNU-stack,"",@progbits
