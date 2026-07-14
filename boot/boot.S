/* ZenovOS FAT12 bootloader.
 * Derived from x16-PRos bootloader by PRoX2011, MIT License.
 * Modifications Copyright (c) 2026 xemoll / Zenov contributors.
 */
.intel_syntax noprefix
.code16
.section .text
.global _start

_start:
    jmp main
    nop

bpbOEM:               .ascii "ZENOVOS "
bpbBytesPerSector:    .word 512
bpbSectorsPerCluster: .byte 1
bpbReservedSectors:   .word 1
bpbNumberOfFATs:      .byte 2
bpbRootEntries:       .word 224
bpbTotalSectors:      .word 2880
bpbMedia:             .byte 0xf0
bpbSectorsPerFAT:     .word 9
bpbSectorsPerTrack:   .word 18
bpbHeadsPerCylinder:  .word 2
bpbHiddenSectors:     .long 0
bpbTotalSectorsBig:   .long 0
bsDriveNumber:        .byte 0
bsUnused:             .byte 0
bsExtBootSignature:   .byte 0x29
bsSerialNumber:       .long 0x5a454e4f
bsVolumeLabel:        .ascii "ZENOVOS    "
bsFileSystem:         .ascii "FAT12   "

print:
    lodsb
    test al, al
    jz .Lprint_done
    mov ah, 0x0e
    int 0x10
    jmp print
.Lprint_done:
    ret

absoluteSector: .byte 0
absoluteHead:   .byte 0
absoluteTrack:  .byte 0

cluster_lba:
    sub ax, 2
    xor cx, cx
    mov cl, byte ptr [bpbSectorsPerCluster]
    mul cx
    add ax, word ptr [datasector]
    ret

lba_to_chs:
    xor dx, dx
    div word ptr [bpbSectorsPerTrack]
    inc dl
    mov byte ptr [absoluteSector], dl
    xor dx, dx
    div word ptr [bpbHeadsPerCylinder]
    mov byte ptr [absoluteHead], dl
    mov byte ptr [absoluteTrack], al
    ret

read_sectors:
.Lread_main:
    mov di, 5
.Lread_retry:
    push ax
    push bx
    push cx
    call lba_to_chs
    mov ah, 0x02
    mov al, 0x01
    mov ch, byte ptr [absoluteTrack]
    mov cl, byte ptr [absoluteSector]
    mov dh, byte ptr [absoluteHead]
    mov dl, byte ptr [bsDriveNumber]
    int 0x13
    jnc .Lread_success
    xor ax, ax
    int 0x13
    dec di
    pop cx
    pop bx
    pop ax
    jnz .Lread_retry
    int 0x18
.Lread_success:
    pop cx
    pop bx
    pop ax
    add bx, word ptr [bpbBytesPerSector]
    jnc .Lread_next
    mov dx, es
    cmp dx, 0x2000
    je failure
    int 0x18
.Lread_next:
    inc ax
    loop .Lread_main
    ret

main:
    cli
    mov ax, 0x07c0
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov ss, ax
    mov sp, 0xffff
    sti
    mov byte ptr [bsDriveNumber], dl
    mov si, OFFSET FLAT:msgLoading
    call print

load_root:
    xor cx, cx
    xor dx, dx
    mov ax, 0x20
    mul word ptr [bpbRootEntries]
    div word ptr [bpbBytesPerSector]
    xchg ax, cx
    mov al, byte ptr [bpbNumberOfFATs]
    mul word ptr [bpbSectorsPerFAT]
    add ax, word ptr [bpbReservedSectors]
    mov word ptr [datasector], ax
    add word ptr [datasector], cx
    mov bx, 0x0200
    call read_sectors

    mov cx, word ptr [bpbRootEntries]
    mov di, 0x0200
.Lfind_kernel:
    push cx
    mov cx, 11
    mov si, OFFSET FLAT:imageName
    push di
    rep cmpsb
    pop di
    je load_fat
    pop cx
    add di, 0x20
    loop .Lfind_kernel
    jmp failure

load_fat:
    mov dx, word ptr [di + 0x1a]
    mov word ptr [cluster], dx
    xor ax, ax
    mov al, byte ptr [bpbNumberOfFATs]
    mul word ptr [bpbSectorsPerFAT]
    mov cx, ax
    mov ax, word ptr [bpbReservedSectors]
    mov bx, 0x0200
    call read_sectors
    mov ax, 0x2000
    mov es, ax
    xor bx, bx
    push bx

load_image:
    mov ax, word ptr [cluster]
    pop bx
    call cluster_lba
    xor cx, cx
    mov cl, byte ptr [bpbSectorsPerCluster]
    call read_sectors
    push bx
    mov ax, word ptr [cluster]
    mov cx, ax
    mov dx, ax
    shr dx, 1
    add cx, dx
    mov bx, 0x0200
    add bx, cx
    mov dx, word ptr [bx]
    test ax, 1
    jnz .Lodd_cluster
    and dx, 0x0fff
    jmp .Lcluster_done
.Lodd_cluster:
    shr dx, 4
.Lcluster_done:
    mov word ptr [cluster], dx
    cmp dx, 0x0ff0
    jb load_image

    mov si, OFFSET FLAT:msgDone
    call print
    .byte 0xea
    .word 0x0000
    .word 0x2000

failure:
    mov si, OFFSET FLAT:msgFailure
    call print
    xor ah, ah
    int 0x16
    int 0x19
    jmp .

datasector: .word 0
cluster:    .word 0
imageName:  .ascii "KERNEL  BIN"
msgLoading: .asciz "\r\nZenovOS loading"
msgDone:    .asciz ".\r\n"
msgFailure: .asciz "\r\nKERNEL.BIN missing. Press a key.\r\n"

.org 510
.word 0xaa55
