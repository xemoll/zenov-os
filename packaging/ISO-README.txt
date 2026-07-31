ZenovOS 0.1.1 BIOS VM ISO
==========================

This disc is a bootable ISO 9660 image for legacy x86 BIOS virtual machines.
It uses El Torito 1.44 MiB floppy emulation and boots the same verified FAT12
ZenovOS loader as the raw .img build.

Virtual-machine settings
------------------------

Architecture:   32-bit x86 / i686
Firmware:       Legacy BIOS (UEFI is not supported yet)
Memory:         32 MiB minimum; 64 MiB recommended
Boot medium:    ZenovOS-0.1.1-x86.iso as a virtual CD/DVD
Persistent disk: ZenovOS-0.1.1-data.img as an IDE hard disk
Boot order:     Optical drive first

The ISO is read-only. ZenovFS applications, settings, package state, audit
records and user files are stored on the separate writable data image. Keep a
backup of that image before destructive testing.

QEMU example
------------

qemu-system-i386 \
  -m 64M \
  -drive file=ZenovOS-0.1.1-data.img,format=raw,if=ide,index=0,media=disk \
  -drive file=ZenovOS-0.1.1-x86.iso,format=raw,media=cdrom,if=ide,index=2,readonly=on \
  -boot order=d,strict=on

The ISO is intended for QEMU, VirtualBox, VMware and other hypervisors that
support legacy BIOS El Torito booting. Physical hardware and UEFI boot remain
unsupported in this build.
