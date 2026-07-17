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
    jne .fail_layout
    mov esi, [esp + 4]
    mov ebx, [esi + 4]
    cmp dword ptr [ebx], 0x68706c61
    jne .fail_layout
    cmp byte ptr [ebx + 4], 'a'
    jne .fail_layout
    cmp byte ptr [ebx + 5], 0
    jne .fail_layout
    mov ebx, [esi + 8]
    cmp dword ptr [ebx], 0x61746562
    jne .fail_layout
    cmp byte ptr [ebx + 4], 0
    jne .fail_layout

    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:stage_layout
    mov ecx, stage_layout_end - stage_layout
    int 0x80

    mov eax, 255
    int 0x80
    cmp eax, ERROR_UNSUPPORTED
    jne .fail_unsupported
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:stage_unsupported
    mov ecx, stage_unsupported_end - stage_unsupported
    int 0x80

    mov eax, SYS_WRITE
    mov ebx, 0x8000
    mov ecx, 1
    int 0x80
    cmp eax, ERROR_FAULT
    jne .fail_pointer
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:stage_unmapped
    mov ecx, stage_unmapped_end - stage_unmapped
    int 0x80

    mov eax, SYS_FILE_READ
    mov ebx, OFFSET FLAT:known_file
    xor ecx, ecx
    mov edx, 1
    int 0x80
    cmp eax, ERROR_FAULT
    jne .fail_pointer
    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:stage_readonly
    mov ecx, stage_readonly_end - stage_readonly
    int 0x80

    mov eax, SYS_WRITE
    mov ebx, OFFSET FLAT:success
    mov ecx, success_end - success
    int 0x80
    xor ebx, ebx
    mov eax, SYS_EXIT
    int 0x80

.fail_layout:
    mov ebx, OFFSET FLAT:failure_layout
    mov ecx, failure_layout_end - failure_layout
    jmp .fail
.fail_unsupported:
    mov ebx, OFFSET FLAT:failure_unsupported
    mov ecx, failure_unsupported_end - failure_unsupported
    jmp .fail
.fail_pointer:
    mov ebx, OFFSET FLAT:failure_pointer
    mov ecx, failure_pointer_end - failure_pointer
.fail:
    mov eax, SYS_WRITE
    int 0x80
    mov ebx, 1
    mov eax, SYS_EXIT
    int 0x80

.section .rodata,"a"
known_file:
    .asciz "/data/docs/readme.txt"
stage_layout:
    .ascii "PROCESS_ARGV_LAYOUT_STAGE_OK\n"
stage_layout_end:
stage_unsupported:
    .ascii "SYSCALL_UNSUPPORTED_STAGE_OK\n"
stage_unsupported_end:
stage_unmapped:
    .ascii "SYSCALL_UNMAPPED_POINTER_STAGE_OK\n"
stage_unmapped_end:
stage_readonly:
    .ascii "SYSCALL_READONLY_POINTER_STAGE_OK\n"
stage_readonly_end:
success:
    .ascii "PROCESS_ARGV_OK\nSYSCALL_ERRORS_OK\nSYSCALL_POINTER_GUARD_OK\n"
success_end:
failure_layout:
    .ascii "PROCESS_ARGV_LAYOUT_FAIL\n"
failure_layout_end:
failure_unsupported:
    .ascii "SYSCALL_UNSUPPORTED_FAIL\n"
failure_unsupported_end:
failure_pointer:
    .ascii "SYSCALL_POINTER_GUARD_FAIL\n"
failure_pointer_end:
.section .note.GNU-stack,"",@progbits
