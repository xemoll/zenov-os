# ZenovOS 0.1.1 verified package cache

ZenPkg uses a persistent verified object cache between repository transport and installation. The cache is not a trust root. Every object remains subordinate to the active signed ZenRepo target metadata.

## Commands

```text
pkg fetch <name>
pkg cache status
pkg cache verify
pkg cache clean
pkg install <name>
```

`pkg fetch` resolves the latest signed target and acquires it through the currently available repository transport. ZenovOS 0.1.1 uses the offline `/data/packages` transport. A future HTTPS transport must write through the same staging and commit API.

`pkg install <name>` always selects a verified cache object. If the object is absent, ZenPkg fetches and commits it first. Direct path installation remains available for diagnostic and recovery workflows, but repository-name installation never treats an unverified transport file as its final source.

## Object paths

Cache objects are addressed by the first 96 bits of the signed package SHA-256:

```text
/var/cache/zp/<24 lowercase hex characters>.part
/var/cache/zp/<24 lowercase hex characters>.zpk
```

The shortened identifier is only a filesystem key. Every lookup rechecks the complete 256-bit digest from signed target metadata, so a prefix collision cannot authorize the wrong object.

## Fetch transaction

A fetch is committed in this order:

1. Resolve a target from the verified ZenRepo delegation.
2. Read the transport source into the bounded 64 KiB package buffer.
3. Verify the complete ZENPKG1 structure, package length, full package SHA-256, manifest identity, entrypoint, payload type and payload SHA-256 against the signed target.
4. Write the bytes to a protected `.part` object.
5. Synchronize ZenovFS metadata.
6. Re-read and fully verify the `.part` object.
7. Atomically rename `.part` to `.zpk`.
8. Synchronize metadata again.
9. Re-read and fully verify the final object.

A failed or interrupted transfer cannot become installable merely by existing in the cache. Only the final `.zpk` path is selectable, and selection repeats the complete verification.

## Protection boundary

`/var/cache/zp` is protected from ordinary shell and userspace write, remove, copy and rename operations. Internal cache mutation is exposed only through package-manager-specific storage entrypoints that require normalized cache object paths.

Cache corruption detected during boot prevents the package manager from becoming ready. `pkg cache verify` performs the same full checks on demand. `pkg cache clean` removes known final and partial objects and synchronizes the filesystem before reporting success.

## Persistence and evidence

The mandatory ZenPkg QEMU workflow uses one persistent data image across five boots. It verifies:

- an empty cache on initial boots;
- one atomic fetch commit for the signed `hello-native` target;
- installation from the cache rather than directly from the transport source;
- successful execution with the signed syscall profile;
- cache and installed-state persistence after reboot;
- cache verification after reboot;
- package removal followed by cache cleanup;
- an empty cache and absent package after the final reboot;
- ZenovFS and ZenovGuard audit verification throughout the lifecycle.

Serial, QEMU monitor and stderr logs are uploaded as workflow evidence.

## Current limits

- Transport is offline; TCP/IP, DNS, TLS and resumable network transfer are not yet implemented.
- ZenovFS1 limits every cached object to 64 KiB.
- Cache enumeration is derived from currently signed targets. Garbage collection of objects from retired repository metadata requires a future persistent cache index.
- There is no concurrent downloader because ZenovOS currently supports one foreground process.
