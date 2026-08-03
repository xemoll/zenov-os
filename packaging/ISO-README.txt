ZenovOS 0.1.1 Self-Contained Live ISO
=====================================

This is the only current ZenovOS launch image. Attach
ZenovOS-0.1.1-x86.iso to a virtual CD/DVD drive, select Legacy BIOS, and boot.
No VDI, QCOW2, VMDK, raw data image, preparation script, or second virtual disk
is required.

Automatic startup
-----------------

The ISO boots the verified FAT12 ZenovOS loader through BIOS El Torito, loads
the kernel, expands the embedded ZenovFS system image, creates a writable
RAM-backed overlay, initializes security policies and applications, and opens
the graphical desktop automatically.

Recommended virtual-machine settings
------------------------------------

Architecture:  32-bit x86 / i686
Firmware:      Legacy BIOS
Memory:        64 MiB or more
Processors:    1
Display:       Standard VGA / VBoxVGA-compatible
Boot medium:   ZenovOS-0.1.1-x86.iso as virtual CD/DVD
Boot order:    Optical drive first
Networking:    Optional; not required for startup
Hard disk:     Not required

QEMU example
------------

qemu-system-i386 \
  -machine pc,vmport=off \
  -m 64M \
  -vga std \
  -drive file=ZenovOS-0.1.1-x86.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on \
  -boot order=d,strict=on

Live-session storage
--------------------

Files and settings are writable during the running session. They are held in a
RAM overlay and reset after power-off or reboot. Persistent installation is a
separate future feature; the current release intentionally ships only one ISO.

Compatibility boundary
----------------------

The guest supports Legacy BIOS El Torito boot. UEFI and physical-hardware
installation are not implemented yet. VirtualBox still requires AMD-V/VT-x to
be enabled and available to VirtualBox; that host-side requirement cannot be
changed by any guest ISO. QEMU TCG can run the ISO without hardware
virtualization.
