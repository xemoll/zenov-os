# ZenovOS 0.1.1 documentation

## Current distribution

- [`LIVE_ISO_0.1.1.md`](LIVE_ISO_0.1.1.md) — self-contained ISO architecture, embedded ZenovFS, writable RAM overlay, no-disk boot contract and verification boundary.
- [`releases/v0.1.1-live1.md`](releases/v0.1.1-live1.md) — current one-file Live ISO download and startup guide.

## Desktop and applications

- [`DESKTOP_0.1.1.md`](DESKTOP_0.1.1.md) — adaptive native desktop, verified VBE modes, scaling, controls and framebuffer evidence.
- [`SYSTEM_APPS_0.1.1.md`](SYSTEM_APPS_0.1.1.md) — Notes, Tasks, Calendar and Clock applications.
- [`PRODUCTIVITY_UTILITIES_0.1.1.md`](PRODUCTIVITY_UTILITIES_0.1.1.md) — Calculator, Reminders, Agenda and local alarms.
- [`ABI_0.1.1.md`](ABI_0.1.1.md) — application memory, stack, syscall and executable-format contract.
- [`ZEX_ABI.md`](ZEX_ABI.md) and [`ELF32_ABI.md`](ELF32_ABI.md) — executable-format references.

## Storage, packages and runtime

- [`ZENOVFS1_TRANSACTIONS.md`](ZENOVFS1_TRANSACTIONS.md) — interrupted-write ordering and recovery.
- [`ZENPKG_FORMAT_1.md`](ZENPKG_FORMAT_1.md) — deterministic package container and manifest contract.
- [`ZENREPO_OFFLINE_0.1.1.md`](ZENREPO_OFFLINE_0.1.1.md) — signed offline repository roles, rotation, delegation, expiry and anti-rollback state.
- [`ZENPKG_CACHE_0.1.1.md`](ZENPKG_CACHE_0.1.1.md) — protected staging, target verification and atomic cache commit.
- [`NATIVE_PACKAGE_MANAGER_0.1.1.md`](NATIVE_PACKAGE_MANAGER_0.1.1.md) — transactional install, repair, rollback and removal.
- [`PACKAGE_COMPATIBILITY_ARCHITECTURE.md`](PACKAGE_COMPATIBILITY_ARCHITECTURE.md) — provider boundaries and foreign-runtime roadmap.
- [`ZENUNIVERSE_REPOSITORY_0.1.1.md`](ZENUNIVERSE_REPOSITORY_0.1.1.md) — platform-neutral artifact catalog.
- [`RUNTIME_PROVIDER_ABI_0.1.1.md`](RUNTIME_PROVIDER_ABI_0.1.1.md) — runtime architecture and capability gates.

## Security

- [`SECURITY_MODEL_0.1.1.md`](SECURITY_MODEL_0.1.1.md) — enforced trust boundaries and non-goals.
- [`SYSCALL_CAPABILITIES_0.1.1.md`](SYSCALL_CAPABILITIES_0.1.1.md) — signed per-application syscall masks and file scopes.
- [`ZCAP_0.1.1.md`](ZCAP_0.1.1.md) — ZCAP1 format and update boundary.
- [`ZENOVGUARD_0.1.1.md`](ZENOVGUARD_0.1.1.md) — appraisal, signed policy, audit and quarantine.
- [`ANTIMALWARE_0.1.1.md`](ANTIMALWARE_0.1.1.md) — ZMID1 intelligence and pre-write prevention.
- [`ON_ACCESS_PROTECTION_0.1.1.md`](ON_ACCESS_PROTECTION_0.1.1.md) — synchronous read mediation.
- [`VERIFIED_READS_0.1.1.md`](VERIFIED_READS_0.1.1.md) — signed ZVRT1 path-bound commitments.
- [`RANSOMWARE_DEFENSE_0.1.1.md`](RANSOMWARE_DEFENSE_0.1.1.md) — ZRWP1 controlled-folder policy.
- [`ZGDB_0.1.1.md`](ZGDB_0.1.1.md) — signed database and revocation.
- [`AUDIT_JOURNAL_0.1.1.md`](AUDIT_JOURNAL_0.1.1.md) — persistent audit format and recovery matrix.
- [`TPM2_TIS_NV_COUNTER_0.1.1.md`](TPM2_TIS_NV_COUNTER_0.1.1.md) — TPM 2.0 TIS transport and NV counter lifecycle.

## Architecture and roadmap

- [`SOURCE_ARCHITECTURE.md`](SOURCE_ARCHITECTURE.md) — source composition and scale contracts.
- [`POST_MERGE_HARDENING.md`](POST_MERGE_HARDENING.md) — process-window scrub and W^X hardening.
- [`ROADMAP_0.1.1.md`](ROADMAP_0.1.1.md) — completed contracts and remaining work.
- [`RELEASE_CHECKLIST_0.1.1.md`](RELEASE_CHECKLIST_0.1.1.md) — final-main verification gate.

## Historical VM distributions

The following documents describe immutable older releases and are not the current launch path:

- [`VM_APPLIANCES_0.1.1.md`](VM_APPLIANCES_0.1.1.md)
- [`releases/v0.1.1-vm4.md`](releases/v0.1.1-vm4.md)
- [`releases/v0.1.1-vm3.md`](releases/v0.1.1-vm3.md)
- [`releases/v0.1.1-vm2.md`](releases/v0.1.1-vm2.md)
- [`releases/v0.1.1-vm1.md`](releases/v0.1.1-vm1.md)
- [`releases/v0.1.1.md`](releases/v0.1.1.md)
