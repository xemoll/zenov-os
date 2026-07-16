.intel_syntax noprefix
.section .text,"ax"
.code32
.global _start

.equ SYS_EXIT, 0
.equ SYS_WRITE_CONSOLE, 1
.equ SYS_FILE_READ, 3
.equ SYS_FILE_WRITE, 4
.equ SYS_FILE_STAT, 5
.equ SYS_VERSION, 6
.equ SYS_SYNC, 7

_start:
    mov eax, SYS_VERSION
    mov ebx, OFFSET FLAT:version_buffer
    mov ecx, 16
    int 0x80
    cmp eax, 0xffffffff
    je .failed
    mov edi, eax

    mov eax, SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:start_message
    mov ecx, start_message_end - start_message
    int 0x80
    mov eax, SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:version_buffer
    mov ecx, edi
    int 0x80
    mov eax, SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:newline
    mov ecx, 1
    int 0x80

    mov eax, SYS_FILE_WRITE
    mov ebx, OFFSET FLAT:file_path
    mov ecx, OFFSET FLAT:file_payload
    mov edx, file_payload_end - file_payload
    xor esi, esi
    int 0x80
    cmp eax, file_payload_end - file_payload
    jne .failed

    mov eax, SYS_FILE_STAT
    mov ebx, OFFSET FLAT:file_path
    mov ecx, OFFSET FLAT:file_info
    int 0x80
    test eax, eax
    jne .failed
    cmp dword ptr [file_info], 1
    jne .failed
    cmp dword ptr [file_info + 4], file_payload_end - file_payload
    jne .failed

    mov eax, SYS_FILE_READ
    mov ebx, OFFSET FLAT:file_path
    mov ecx, OFFSET FLAT:file_buffer
    mov edx, 128
    int 0x80
    cmp eax, file_payload_end - file_payload
    jne .failed

    cld
    mov esi, OFFSET FLAT:file_payload
    mov edi, OFFSET FLAT:file_buffer
    mov ecx, file_payload_end - file_payload
    repe cmpsb
    jne .failed

    mov eax, SYS_SYNC
    int 0x80
    test eax, eax
    jne .failed

    mov eax, SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:file_buffer
    mov ecx, file_payload_end - file_payload
    int 0x80
    mov eax, SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:success_message
    mov ecx, success_message_end - success_message
    int 0x80
    xor ebx, ebx
    mov eax, SYS_EXIT
    int 0x80

.failed:
    mov eax, SYS_WRITE_CONSOLE
    mov ebx, OFFSET FLAT:failure_message
    mov ecx, failure_message_end - failure_message
    int 0x80
    mov ebx, 1
    mov eax, SYS_EXIT
    int 0x80

.hang:
    ud2
    jmp .hang

.section .rodata,"a"
start_message:
    .ascii "FILEIO_ELF_START ZenovOS "
start_message_end:
newline:
    .ascii "\n"
file_path:
    .asciz "/data/apps/userio.txt"
file_payload:
    .ascii "FILE_SYSCALL_PERSIST_OK\n"
file_payload_end:
success_message:
    .ascii "FILEIO_ELF_OK\n"
success_message_end:
failure_message:
    .ascii "FILEIO_ELF_FAIL\n"
failure_message_end:

.section .bss,"aw",@nobits
.align 16
version_buffer:
    .skip 16
file_info:
    .skip 12
file_buffer:
    .skip 128
