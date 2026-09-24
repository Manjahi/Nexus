# Secure Vault — threat model

Status: Draft, written before implementation per the plan's Phase 7 prerequisite
(`docs/IMPLEMENTATION_PLAN.md`). Revisit once `nexuspc-vault` ships and again
before any external security review.

## Asset

The vault's decrypted contents: passwords, secure notes, and any other secret
fields a user stores. Everything else NexusPC touches (hardware metrics,
network samples, indexed file text, backup metadata) is out of scope for this
document — see the modules' own repositories for that data's handling.

## Trust boundary

Spec section 2 states the rule plainly: *"The Vault is a security boundary.
Other NexusPC modules MUST NOT be able to read decrypted vault records merely
because they run in the same suite."* Concretely:

- The vault's encrypted store is its own file, never a table in the shared
  `nexuspc.db` (spec section 7's explicit rule).
- Decrypted vault entries exist only inside the `nexuspc-vault` process, only
  while unlocked, and only for as long as needed to serve one request.
- The desktop UI (`nexuspc-ui`) is **not trusted with plaintext at rest**: it
  requests decrypted values over IPC for display/copy and never persists them
  (no logs, no crash dumps with secret material, no settings blobs).
- Other modules (storage, backup, search, network, hardware, connectivity)
  have no code path to the vault at all — there is no shared repository, no
  shared connection, no event carrying vault data on the app's event bus.

## Adversaries considered (in scope)

1. **Another local process reading NexusPC's files at rest** (e.g. malware
   with the user's own privileges, or someone with filesystem access to a
   powered-off/stolen disk). Mitigation: the vault file is fully encrypted;
   the KDF is deliberately slow (Argon2id) so an offline guess against a
   stolen file is expensive per attempt.
2. **A bug in a non-vault NexusPC module** (storage scanner, search indexer,
   etc.) leaking data it was never given. Mitigation: process isolation — a
   bug in `nexuspc-ui` or a module cannot read `nexuspc-vault`'s heap.
3. **Vault plaintext lingering in `nexuspc-ui` memory or on screen/clipboard
   longer than necessary.** Mitigation: the UI never stores a decrypted value
   beyond what's needed to render it once; clipboard copies of a secret are
   cleared automatically after a short timeout (spec section 2: "Clipboard
   timeout"); the vault auto-locks after inactivity independent of the rest
   of the app.
4. **A crash or core dump exposing decrypted memory.** Mitigation: secret
   buffers in `nexuspc-vault` use `sodium_malloc`/`sodium_mlock` (best-effort
   on Windows) and are zeroed as soon as they're no longer needed
   (`sodium_memzero` on drop).
5. **A weak or reused master password.** Mitigation: Argon2id with parameters
   chosen for interactive unlock (~0.5-1s on typical hardware) makes offline
   brute force expensive; the vault's own "health" report flags weak/reused/
   old entries within the vault itself (the master password's strength is the
   user's responsibility — NexusPC can only nudge, not enforce, without a
   backend to check against).

## Explicitly out of scope (for this milestone)

- **Remote/network attackers.** The vault process only ever listens on a
  local named pipe (spec section 5, UFR-012: no remote management by
  default). No network-facing vault API exists or is planned.
- **A compromised OS kernel or an attacker with admin/SYSTEM rights on the
  same machine.** At that privilege level the attacker can read any
  process's memory, defeating in-memory protections regardless of vault
  design. `sodium_mlock` reduces the swap-to-disk window but is not a
  defense against a privileged local attacker.
- **Physical/hardware attacks** (cold boot, DMA), side-channel timing
  attacks against the KDF/cipher implementation, and supply-chain compromise
  of libsodium itself. Standard, well-reviewed primitives (Argon2id,
  XChaCha20-Poly1305 via libsodium) are used specifically so this project
  does not need to reason about primitive-level cryptanalysis.
- **Multi-user / shared-machine isolation** beyond OS file permissions. The
  vault file inherits normal filesystem ACLs; NexusPC does not add its own
  access-control layer on top.

## Consequences for the design (ADR-0003)

- Vault storage: its own versioned encrypted file format, never the shared
  SQLite database.
- Process split: `nexuspc-vault` as a separate OS process from `nexuspc-ui`,
  communicating over a local named pipe with a minimal request/response API
  (unlock, lock, list, get, put, delete, generate-password, health).
- The IPC API is deliberately small: fewer verbs means a smaller surface to
  review. No verb ever returns more plaintext than the UI is about to render.
- Independent auto-lock timer inside `nexuspc-vault`, not driven by the UI
  process (a compromised or frozen UI must not be able to keep the vault
  unlocked indefinitely).

## Review checklist (before treating the vault as production-ready)

- [x] **KDF parameters re-validated against current OWASP/libsodium
      guidance, 2026-09-21.** `KdfParams::interactive()` uses libsodium
      1.0.22's Argon2id `OPSLIMIT_INTERACTIVE`/`MEMLIMIT_INTERACTIVE`:
      opslimit (t)=2, memlimit (m)=64 MiB, parallelism fixed at 1 (libsodium's
      `crypto_pwhash` API doesn't expose `p`). Compared against the current
      OWASP Password Storage Cheat Sheet's Argon2id options (as of this
      review): m=19 MiB/t=2/p=1 and m=46 MiB/t=1/p=1 are the two p=1
      baselines it lists. 64 MiB/t=2/p=1 exceeds both in every dimension, and
      single-lane Argon2id is if anything *more* resistant to parallel
      (GPU/ASIC) attack per unit of memory than a p=4 profile, since there's
      no parallelism for an attacker's hardware to exploit. Verdict: adequate
      for v1's ~0.5-1s interactive-unlock UX target. Follow-up (not
      implemented here - no UI to opt into it yet): a `KdfParams::moderate()`
      profile (opslimit=3, memlimit=256 MiB, libsodium's own "moderate"
      preset) as a user-selectable stronger option for people willing to
      trade unlock speed for a harder-to-brute-force file; the header already
      stores whatever KDF params a vault was created with, so this is
      forward-compatible without a format change.
- [x] **Fuzzed the vault file parser, 2026-09-21** - `tests/unit/vault/
      test_vault_file.cpp`'s `"read_header and unlock reject malformed files
      without crashing"` test: structural edge cases (empty file, truncated
      at every field boundary, oversized claimed salt length, ciphertext
      shorter than the AEAD tag) plus 3000 rounds of randomized
      mutation (byte flips, truncation, appended garbage, contiguous
      overwrites) of a real vault file's bytes fed to `read_header()`, plus a
      smaller 150-round sample against `unlock()`. **This found a real
      issue, now fixed**: `parse_file()` read the header's `opslimit`/
      `memlimit` fields and `unlock()` passed them straight to
      `derive_key()`/`crypto_pwhash()` with no upper bound - libsodium
      accepts an opslimit up to ~4 billion and a memlimit up to several TiB,
      so a corrupted or maliciously crafted vault file (e.g. from an
      untrusted backup, this document's own example adversary) could force
      an unlock attempt to run for an extremely long time or attempt a huge
      allocation, before the AEAD tag - the actual authentication check - is
      ever reached. Fixed in `apps/vault/lib/src/vault_file.cpp`:
      `parse_file()` now rejects any header claiming opslimit/memlimit above
      `kMaxAcceptedOpslimit`/`kMaxAcceptedMemlimit` (4x/2x
      `KdfParams::interactive()`'s values - enough headroom for a future
      stronger profile, nowhere near enough to be a resource-exhaustion
      vector) before the file is treated as parseable at all, i.e. strictly
      before any KDF work happens.
- [x] **Confirmed `sodium_mlock` takes effect on this Windows version,
      2026-09-21** - `tests/unit/crypto/test_crypto.cpp`'s `"sodium_mlock
      succeeds on this platform"` test calls the primitive directly (`sodium_
      mlock`/`sodium_munlock` on a 4 KiB buffer) rather than assuming success
      from documentation. Passed on Windows 10 Pro 10.0.19045 (this
      project's dev machine): `VirtualLock` is not being refused by the
      process's working-set quota for a buffer this small. `SecureBuffer`
      itself never learns whether `sodium_malloc`'s internal `sodium_mlock`
      call succeeded (libsodium doesn't surface that through the allocation's
      return value), which *is* the "fails safe" property this item asked to
      confirm: a locked-memory failure can never crash the vault or block an
      allocation, only silently weaken the non-swap guarantee. Re-run this
      check if it ever starts failing in CI - that would mean the runner's
      working-set quota changed, worth knowing about.
- [ ] Independent read-through of this document and the vault code by
      someone other than its author, before calling M7 "done" in the
      portfolio sense. **Still open** - the other three items are the
      self-auditable prep for this one; this one specifically needs a second
      person and can't be closed solo. See
      `docs/security/vault-review-guide.md` for a structured walkthrough a
      reviewer can follow (component-by-component, what to check in each,
      build/test instructions, a findings template) - written 2026-09-24 to
      make this item actually actionable for someone with no prior context
      on this codebase.
