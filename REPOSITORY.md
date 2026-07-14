# Repository boundary

This repository owns ZenovOS: boot code, kernel `.zv` source, OS runtime,
drivers, filesystems, shell, applications, disk images, QEMU checks and release
artifacts.

The separate `xemoll/zenov` repository owns the Zenov language, compiler,
runtime, standard library, target definitions and reusable freestanding APIs.

ZenovOS consumes released or pinned Zenov toolchains. It must not be embedded
as a source subtree in the Zenov compiler repository.
