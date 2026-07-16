.intel_syntax noprefix
.section .text,"ax"
.code32
.extern syscall_dispatch

.global user_enter
.global syscall_entry
.global saved_kernel_esp

user_enter:
    # user_enter is called through the i386 C ABI. The application may freely
    # modify its registers, so preserve all callee-saved kernel registers before
    # storing the return stack.
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
    jnz .return_to_kernel

    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

.return_to_kernel:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
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
