.intel_syntax noprefix
.section .text,"ax"
.code32
.extern syscall_dispatch
.extern user_window_cleanup

.global user_enter
.global syscall_entry
.global user_abort_entry
.global saved_kernel_esp

user_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_kernel_esp], esp
    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov cx, 0x23
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    push 0x23
    push edx
    pushfd
    pop ecx
    or ecx, 0x200
    push ecx
    push 0x1B
    push eax
    iretd

syscall_entry:
    cld
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_dispatch
    add esp, 4
    test eax, eax
    jnz user_abort_entry
    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

user_abort_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    call user_window_cleanup
    mov esp, [saved_kernel_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    sti
    ret

.section .data,"aw"
saved_kernel_esp:
    .long 0

.section .note.GNU-stack,"",@progbits
