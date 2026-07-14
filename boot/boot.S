/* ZenovOS BIOS FAT12 boot sector.
 * FAT12 loader lineage: x16-PRos by PRoX2011 (MIT); see THIRD_PARTY.md.
 */
.intel_syntax noprefix
.code16
.section .text
.global _start

_start:
    jmp short boot_start
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
bsSerialNumber:       .long 0x5a4f5331
bsVolumeLabel:        .ascii "ZENOVOS    "
bsFileSystem:         .ascii "FAT12   "

boot_start:
    jmp main

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print
.done:
    ret

absoluteSector: .byte 0
absoluteHead:   .byte 0
absoluteTrack:  .byte 0

cluster_lba:
    sub ax, 2
    xor cx, cx
    mov cl, byte ptr [bpbSectorsPerCluster]
    mul cx
    add ax, word ptr [dataSector]
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
.next_sector:
    mov di, 5
.retry:
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
    jnc .success
    xor ax, ax
    int 0x13
    dec di
    pop cx
    pop bx
    pop ax
    jnz .retry
    jmp failure
.success:
    pop cx
    pop bx
    pop ax
    add bx, word ptr [bpbBytesPerSector]
    jc failure
    inc ax
    loop .next_sector
    ret

main:
    cli
    mov ax, 0x07c0
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov byte ptr [bsDriveNumber], dl
    mov si, OFFSET FLAT:msgLoading
    call print

    xor cx, cx
    xor dx, dx
    mov ax, 0x20
    mul word ptr [bpbRootEntries]
    div word ptr [bpbBytesPerSector]
    xchg ax, cx
    mov al, byte ptr [bpbNumberOfFATs]
    mul word ptr [bpbSectorsPerFAT]
    add ax, word ptr [bpbReservedSectors]
    mov word ptr [dataSector], ax
    add word ptr [dataSector], cx
    mov bx, 0x0200
    call read_sectors

    mov cx, word ptr [bpbRootEntries]
    mov di, 0x0200
.find_kernel:
    push cx
    mov cx, 11
    mov si, OFFSET FLAT:imageName
    push di
    rep cmpsb
    pop di
    je load_fat
    pop cx
    add di, 0x20
    loop .find_kernel
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

    mov ax, 0x1000
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
    jnz .odd_cluster
    and dx, 0x0fff
    jmp .cluster_done
.odd_cluster:
    shr dx, 4
.cluster_done:
    mov word ptr [cluster], dx
    cmp dx, 0x0ff0
    jb load_image

    mov si, OFFSET FLAT:msgDone
    call print
    mov dl, byte ptr [bsDriveNumber]
    .byte 0xea
    .word 0x0000
    .word 0x1000

failure:
    mov si, OFFSET FLAT:msgFailure
    call print
    xor ah, ah
    int 0x16
    int 0x19
    jmp .

dataSector: .word 0
cluster:    .word 0
imageName:  .ascii "KERNEL  BIN"
msgLoading: .asciz "\r\nZenovOS 0.1.0 loading"
msgDone:    .asciz ".\r\n"
msgFailure: .asciz "\r\nBoot failure. Press a key.\r\n"

.org 510
.word 0xaa55
