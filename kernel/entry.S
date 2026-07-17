.intel_syntax noprefix
.section .text.boot,"ax"
.code16
.global kernel_entry16
.extern kernel_main
.extern __bss_start
.extern __bss_end
.extern __kernel_stack_top

.set BOOT_INFO, 0x0500
.set E820_BUFFER, 0x0600
.set BOOT_MAGIC, 0x5A4F5331

kernel_entry16:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7a00
    sti

    mov dword ptr [BOOT_INFO], BOOT_MAGIC
    mov byte ptr [BOOT_INFO + 4], dl
    mov byte ptr [BOOT_INFO + 5], 0
    mov word ptr [BOOT_INFO + 6], 0

    xor ebx, ebx
    mov di, E820_BUFFER
.e820_next:
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    mov dword ptr es:[di + 20], 1
    int 0x15
    jc .e820_done
    cmp eax, 0x534d4150
    jne .e820_done
    add di, 24
    inc byte ptr [BOOT_INFO + 5]
    cmp byte ptr [BOOT_INFO + 5], 32
    jae .e820_done
    test ebx, ebx
    jne .e820_next
.e820_done:
    cli

    in al, 0x92
    or al, 0x02
    and al, 0xfe
    out 0x92, al

    addr32 lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    data32 ljmp 0x08, protected_entry

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff

    # Ring-3 code: base 0x00400000, byte-granular 1 MiB limit, 32-bit.
    .word 0xffff
    .word 0x0000
    .byte 0x40
    .byte 0xfa
    .byte 0x4f
    .byte 0x00

    # Ring-3 data: same isolated 1 MiB linear window.
    .word 0xffff
    .word 0x0000
    .byte 0x40
    .byte 0xf2
    .byte 0x4f
    .byte 0x00

.global gdt_tss
gdt_tss:
    .quad 0x0000000000000000
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

.code32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, OFFSET FLAT:__kernel_stack_top
    xor ebp, ebp
    cld

    mov edi, OFFSET FLAT:__bss_start
    mov ecx, OFFSET FLAT:__bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

.section .note.GNU-stack,"",@progbits
