# Repository boundary

`xemoll/zenov-os` owns the operating system: boot path, kernel, drivers,
filesystem image, shell, native tools, tests and release artifacts.

`xemoll/zenov` owns the Zenov language, compiler, runtime contracts and target
ABI. ZenovOS consumes Zenov but does not embed the language repository.

Version `0.1.0` is intentionally developed in place until its protected-mode
foundation, diagnostics and native toolchain are stable. This deep update does
not change the public version number.
