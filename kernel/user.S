.intel_syntax noprefix
.section .text,"ax"
.code32
.extern syscall_dispatch

.global user_enter
.global syscall_entry
.global user_return_to_kernel
.global saved_kernel_esp

user_enter:
    # Called through the i386 C ABI. Applications may modify every register, so
    # preserve the complete callee-saved kernel context before entering ring 3.
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
    jnz user_return_to_kernel

    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

# Shared non-local return used by the exit syscall and by contained ring-3
# exceptions. The interrupted userspace/TSS stack is intentionally abandoned;
# the exact kernel C call frame saved by user_enter is restored instead.
user_return_to_kernel:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov edx, [saved_kernel_esp]
    mov dword ptr [saved_kernel_esp], 0
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    sti
    ret

.section .data,"aw"
saved_kernel_esp:
    .long 0
