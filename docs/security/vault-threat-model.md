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

- [ ] KDF parameters re-validated against current OWASP/libsodium guidance
      at ship time, not just at design time.
- [ ] Fuzz the vault file parser (it will be handed attacker-controlled bytes
      if a vault file is restored from an untrusted backup).
- [ ] Confirm `sodium_mlock` actually takes effect on the target Windows
      version (working-set quota permitting) and fails safe (does not
      silently skip locking) if it can't.
- [ ] Independent read-through of this document and the vault code by
      someone other than its author, before calling M7 "done" in the
      portfolio sense.
