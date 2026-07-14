/* ZenovOS 16-bit BIOS runtime for the stage0 bare-metal target. */

current_color: .byte 0x07
shell_prompt_ptr: .word default_prompt
input_length: .byte 0
input_buffer: .space 64, 0
default_prompt: .asciz "zenov> "
unknown_command: .asciz "Unknown command. Type help."

serial_init:
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x80
    out dx, al
    mov dx, 0x3f8
    mov al, 0x03
    out dx, al
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x03
    out dx, al
    mov dx, 0x3fa
    mov al, 0xc7
    out dx, al
    mov dx, 0x3fc
    mov al, 0x0b
    out dx, al
    ret

serial_write_char:
    push ax
    push dx
    mov ah, al
.Lserial_wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .Lserial_wait
    mov dx, 0x3f8
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

serial_print_string:
    push ax
.Lserial_next:
    lodsb
    test al, al
    jz .Lserial_done
    call serial_write_char
    jmp .Lserial_next
.Lserial_done:
    pop ax
    ret

clear_screen:
    mov ax, 0x0003
    int 0x10
    ret

print_char_raw:
    push ax
    push bx
    mov ah, 0x0e
    xor bh, bh
    mov bl, byte ptr [current_color]
    int 0x10
    pop bx
    pop ax
    call serial_write_char
    ret

print_char:
    cmp al, 0x0a
    jne .Lprint_raw
    push ax
    mov al, 0x0d
    call print_char_raw
    pop ax
.Lprint_raw:
    call print_char_raw
    ret

print_string:
    push ax
.Lprint_next:
    lodsb
    test al, al
    jz .Lprint_done
    call print_char
    jmp .Lprint_next
.Lprint_done:
    pop ax
    ret

print_newline:
    mov al, 0x0a
    call print_char
    ret

print_backspace:
    mov al, 0x08
    call print_char_raw
    mov al, 0x20
    call print_char_raw
    mov al, 0x08
    call print_char_raw
    ret

read_line:
    mov byte ptr [input_length], 0
    mov di, OFFSET FLAT:input_buffer
.Lread_loop:
    xor ah, ah
    int 0x16
    cmp al, 0x0d
    je .Lread_enter
    cmp al, 0x08
    je .Lread_backspace
    cmp al, 0x20
    jb .Lread_loop
    cmp al, 0x7e
    ja .Lread_loop
    mov bl, byte ptr [input_length]
    cmp bl, 63
    jae .Lread_loop
    stosb
    inc byte ptr [input_length]
    call print_char
    jmp .Lread_loop
.Lread_backspace:
    cmp byte ptr [input_length], 0
    je .Lread_loop
    dec byte ptr [input_length]
    dec di
    mov byte ptr [di], 0
    call print_backspace
    jmp .Lread_loop
.Lread_enter:
    mov byte ptr [di], 0
    call print_newline
    ret

strcmp:
.Lstrcmp_next:
    mov al, byte ptr [si]
    mov ah, byte ptr [di]
    cmp al, ah
    jne .Lstrcmp_not_equal
    test al, al
    jz .Lstrcmp_equal
    inc si
    inc di
    jmp .Lstrcmp_next
.Lstrcmp_equal:
    xor ax, ax
    ret
.Lstrcmp_not_equal:
    mov ax, 1
    ret

shell_main:
.Lshell_loop:
    mov si, word ptr [shell_prompt_ptr]
    call print_string
    call read_line
    cmp byte ptr [input_length], 0
    je .Lshell_loop
    mov bx, OFFSET FLAT:command_table
.Lshell_find:
    mov si, word ptr [bx]
    test si, si
    jz .Lshell_unknown
    mov di, OFFSET FLAT:input_buffer
    push bx
    call strcmp
    pop bx
    test ax, ax
    jz .Lshell_execute
    add bx, 5
    jmp .Lshell_find
.Lshell_execute:
    mov al, byte ptr [bx + 4]
    cmp al, 1
    je .Lshell_clear
    cmp al, 2
    je .Lshell_reboot
    cmp al, 3
    je .Lshell_halt
    mov si, word ptr [bx + 2]
    test si, si
    jz .Lshell_loop
    call print_string
    call print_newline
    jmp .Lshell_loop
.Lshell_clear:
    call clear_screen
    jmp .Lshell_loop
.Lshell_reboot:
    int 0x19
    jmp .
.Lshell_halt:
    call halt_cpu
.Lshell_unknown:
    mov si, OFFSET FLAT:unknown_command
    call print_string
    call print_newline
    jmp .Lshell_loop

halt_cpu:
    cli
.Lhalt_loop:
    hlt
    jmp .Lhalt_loop
