# ZenPkg resumable transport contract — ZenovOS 0.1.1

## Scope

This layer moves an already authorized ZenRepo target into the persistent ZenPkg cache. It is a transport state machine, not a new source of trust.

The current provider is the signed offline repository stored under `/data/packages`. DNS, TCP/IP, HTTP and TLS are not implemented by this change and must not be reported as available.

## Trust boundary

The active ZenRepo target supplies the expected:

- package name and version;
- entrypoint and payload type;
- exact package length;
- full package SHA-256;
- payload SHA-256;
- signed syscall policy.

Transport bytes are untrusted until the complete `.part` file passes the existing ZenPkg and ZenRepo checks. A cached object is exposed to installation only after full verification and atomic rename to `.zpk`.

## State machine

1. Resolve the target from verified repository metadata.
2. Reuse a valid final cache object when present.
3. Verify the current offline source against the signed target.
4. Inspect an existing digest-addressed `.part` object.
5. Reject and remove a partial object when its type or length is invalid.
6. Hash the stored partial prefix and compare it with the same prefix of the verified source.
7. Resume from the committed partial length when the prefix matches.
8. Append at most 512 bytes per transaction.
9. Synchronize ZenovFS metadata after every committed chunk.
10. Never allow the partial length to exceed the signed target length.
11. Retry the state-machine attempt at most three times after a storage failure.
12. Verify the complete package, payload and target identity.
13. Atomically rename `.part` to `.zpk`.
14. Verify the final cache object again before installation.

## Persistent evidence markers

The guest serial log emits:

- `ZENPKG_TRANSPORT_BEGIN`;
- `ZENPKG_TRANSPORT_RESUME` when a valid partial prefix exists;
- `ZENPKG_TRANSPORT_CHUNK_COMMIT` after each durable chunk;
- `ZENPKG_TRANSPORT_RETRY` after a failed attempt;
- `ZENPKG_TRANSPORT_COMPLETE` only after full target verification;
- `ZENPKG_TRANSPORT_RETRY_EXHAUSTED` on fail-closed exhaustion.

The ZenPkg workflow requires begin, chunk and complete evidence for the signed `hello-native` target and rejects retry exhaustion.

## Security properties

- `.part` append access is restricted to the package-manager cache writer.
- Ordinary shell and userspace storage operations remain blocked for `/var/cache/zp`.
- Resume cannot authorize a package: final authorization still depends on the full signed target metadata.
- Corrupt or mismatched partial prefixes are removed instead of being continued.
- A short digest in the cache filename is only a storage key; full SHA-256 verification remains mandatory.

## Remaining work

A network provider must supply the same bounded byte stream and preserve this state-machine contract. Before it can be marked available, ZenovOS still needs a real network stack, DNS resolution, TLS certificate and hostname validation, HTTP response framing, Range/Content-Range validation, timeout accounting and mirror policy.
