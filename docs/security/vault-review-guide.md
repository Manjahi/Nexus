# Secure Vault — independent review guide

This is the one open item on the vault's own review checklist
(`docs/security/vault-threat-model.md`'s "Review checklist" section): *an
independent read-through of the threat model and the vault code by someone
other than the author.* Everything else on that checklist is self-audit
work the author already did and documented. This guide exists to make that
read-through efficient for a reviewer who has no prior context on this
codebase — where to start, what each piece is supposed to guarantee, and
what's already been checked so you can spend your time on what hasn't.

You do not need to read the rest of NexusPC to review the vault. The whole
point of the design (below) is that the vault is independent of it.

## 1. Read these first, in order

1. `docs/adr/0003-vault-security-architecture.md` — the decision record:
   why a separate process, why a separate file format, what the IPC surface
   looks like.
2. `docs/security/vault-threat-model.md` — the asset, trust boundary,
   adversaries considered (and explicitly *not* considered), and the
   self-audit checklist this guide closes out.
3. This document.

Total reading time for those two: about 15 minutes. Everything below
points back into the actual source.

## 2. What you're reviewing, in one paragraph

`nexuspc-vault.exe` is a separate OS process from the main NexusPC app. It
owns one file (`vault.nxv`, XChaCha20-Poly1305-AEAD-sealed, keyed by an
Argon2id-derived key from a master password) and talks to the desktop UI
over a single local named pipe with a small JSON verb protocol
(`status`/`create`/`unlock`/`lock`/`list`/`get`/`put`/`delete`/
`generate_password`/`health`/`export`/`shutdown`). No other NexusPC module
has any code path to it. The property under review is: **can a bug
anywhere else in NexusPC, or an attacker who can only reach what other
modules expose, ever get at a decrypted vault entry?** The threat model's
answer is "no, by construction" — your job is to find out if that's
actually true.

## 3. Build and run it yourself first

Don't review this from a diff. Build it, run it, unlock a vault, add an
entry, lock it, look at the file on disk.

```
# From a Developer PowerShell / after calling vcvars64.bat:
cmake --preset windows-msvc
cmake --build --preset debug --target nexuspc_vault nexus_vault_core_tests nexus_ipc_tests
```

- Run `nexuspc-vault.exe` directly (it prints the vault file path it's
  using and blocks in its accept loop — `Ctrl+C` to stop it). Point it at
  a scratch file first: `$env:NEXUSPC_VAULT_PATH = "C:\temp\test.nxv"`
  before launching, so you're not touching a real vault.
- Open the resulting `.nxv` file in a hex viewer. You should see a small
  plaintext header (format version, KDF params, salt — see
  `apps/vault/lib/src/vault_file.cpp`'s `build_header_bytes`) followed by
  a nonce and then ciphertext with no readable structure. If you can
  identify field boundaries, titles, or any plaintext-looking bytes past
  the header, that's a finding.
- Run the test suites: `ctest --preset debug -R "vault|ipc|crypto"` (or
  run `nexus_vault_core_tests.exe`, `nexus_ipc_tests.exe`,
  `nexus_crypto_tests.exe` directly under `build\windows-msvc\tests\unit\
  Debug\`). All should pass. `nexus_vault_core_tests` includes a 3000-round
  randomized-mutation fuzz test of the file parser (`test_vault_file.cpp`)
  — expect it to take a few seconds.
- Run the real end-to-end process test:
  `tests\integration\test_vault_process.cpp`, built as part of
  `nexus_integration_tests`. This one spawns the actual `nexuspc-vault.exe`
  and drives it over a real named pipe — read it before you read the
  component walkthrough below, since it's a working example that
  exercises the whole verb surface in sequence.

## 4. Component walkthrough

For each piece: what it's for, the specific property to check, and what
question to ask yourself. File paths are relative to the repo root.

### 4.1 The crypto primitives — `libs/crypto/`

- `include/nexus/crypto/crypto.hpp` (the whole public API — short, read
  the whole thing) + `src/crypto.cpp`.
- This is a thin wrapper over libsodium (Argon2id KDF, XChaCha20-Poly1305
  AEAD, `sodium_malloc`/`sodium_mlock`/`sodium_memzero`-backed
  `SecureBuffer`, rejection-sampled password generation). It does **not**
  implement any cryptographic primitive itself — that's deliberate (see
  the threat model's "explicitly out of scope" section: primitive-level
  cryptanalysis of Argon2id/XChaCha20-Poly1305/libsodium itself is not
  this project's to re-litigate).
- **Check**: every call site that constructs a nonce, salt, or key uses
  `random_bytes()` (CSPRNG-backed) and never a predictable value. Grep for
  where nonces are generated (`vault_file.cpp`'s `save()`) and confirm a
  fresh nonce is drawn on every save, never reused with the same key —
  nonce reuse under the same key is a total break of this AEAD scheme.
- **Check**: `SecureBuffer` is never copied (its copy constructor/
  assignment are deleted — confirm nothing works around that via
  `.data()`/`.span()` into a container that then gets copied) and that
  every place a raw key or password touches a `std::string`/`std::vector`
  instead of a `SecureBuffer` is either genuinely transient (immediately
  fed to a crypto call and not retained) or a place you think should have
  used `SecureBuffer` and didn't.
- **Already checked** (see the threat model's checklist): KDF parameters
  vs. current OWASP guidance; `sodium_mlock` actually succeeds on this
  platform. You don't need to re-derive these, but spot-checking the
  reasoning is fair game.

### 4.2 The file format — `apps/vault/lib/src/vault_file.{hpp,cpp}`

- `VaultFile::create()`/`unlock()`/`save()`, and the header
  (de)serialization (`build_header_bytes()`/`parse_file()`).
- **Check**: `parse_file()` is the untrusted-input boundary — everything
  it reads came from a file on disk, which could be corrupted, truncated,
  or deliberately crafted (e.g. a malicious "vault backup" someone was
  tricked into restoring). Confirm every length-prefixed field is bounds-
  checked against the actual remaining buffer size before being read, not
  just trusted. `kMaxAcceptedOpslimit`/`kMaxAcceptedMemlimit` (currently 4
  / 128 MiB, vs. `KdfParams::interactive()`'s 2 / 64 MiB) exist precisely
  because this was **not** checked originally — a fuzz test found that an
  attacker-controlled opslimit/memlimit from the header reached
  `crypto_pwhash()` before the AEAD tag was ever verified, meaning a
  corrupted file could force an extremely expensive KDF run or a huge
  allocation. That's fixed; the question worth re-asking is whether
  *every* other header field has the same kind of bound, not just these
  two.
- **Check**: the actual decryption failure path. `aead_decrypt()` returning
  `nullopt` (wrong password, or tampered/corrupted ciphertext) must be
  indistinguishable from each other in whatever `unlock()` returns to its
  caller — confirm there's no timing or error-message difference that
  would let an attacker distinguish "wrong password" from "correct
  password, corrupted file" (the second case implies knowledge that the
  password was right).
- **Check**: `save()`'s write path — it should write to a temp file and
  atomically rename over the real one (confirm this in the code), not
  write in place. A crash or power loss mid-write to the real file would
  otherwise corrupt or truncate the only copy of someone's vault.

### 4.3 The unlock session — `apps/vault/lib/src/vault_store.cpp`

- `VaultStore` holds the decrypted entries and derived key for one unlock
  session, in memory only.
- **Check**: `lock()` and the destructor path — confirm every entry's
  secret fields actually get zeroed (`secure_clear()` in `entry.cpp`) and
  the key's `SecureBuffer` is dropped, not just that the `unlocked_`
  optional is reset (resetting a `std::optional<T>` calls `T`'s
  destructor, so this should be automatic, but confirm `UnlockedVault`'s
  destructor doesn't get short-circuited anywhere, e.g. by an exception
  path that skips normal unwinding).
- **Check**: `export_to()` (the newest addition) — it reuses the
  already-derived key and the live vault's header to seal a second file.
  Confirm it truly never constructs a plaintext copy of the entries on
  disk at any point, and that it refuses to silently overwrite an
  existing file at the destination (both are claimed in the doc comment —
  verify against the actual code).

### 4.4 The wire protocol — `apps/vault/lib/src/protocol.{hpp,cpp}`

- `handle_request()` and each verb handler.
- **Check**: every verb that requires an unlocked vault actually checks
  `store.locked()` first and returns an error otherwise — go through the
  verb list and confirm none of them can be reached in a locked state to
  extract information (even indirectly — e.g. does an error message ever
  leak whether a given `id` exists while locked?).
- **Check**: `handle_list()`'s response never includes `password`/`notes`
  (`summary_to_json()` should only ever serialize non-secret fields) —
  this is the one place a "browse your vault" UI flow is allowed to *not*
  trigger a full decrypt-and-display. Confirm `get()` is the only verb
  that returns a secret field, and that it returns exactly one entry, not
  a way to enumerate all of them.
- **Check**: input validation on `put()` — can a malformed or
  adversarially large `entry` object (e.g. a multi-megabyte string in
  `notes`) cause a problem (unbounded memory growth, a crash) rather than
  either succeeding or failing cleanly?

### 4.5 The process boundary — `apps/vault/src/main.cpp`

- The accept loop, the independent auto-lock thread, and the `shutdown`
  verb.
- **Check**: the auto-lock thread and the request-handling loop both touch
  `store` through the same `store_mutex` — confirm there's no path that
  touches `store` without holding it (a data race here could manifest as
  a use-after-free of the `SecureBuffer` key, which is a much worse bug
  than a logic error).
- **Check**: the auto-lock timer is driven by `last_activity`, updated
  only *after* a real (non-shutdown) request completes — confirm a UI
  that stops sending requests (frozen, killed, or malicious) genuinely
  cannot prevent the lock from firing. This is exactly what the threat
  model calls out as a required property (section 2's "independent
  auto-lock").
- **Check**: nothing in this file (or anything it calls) ever logs,
  prints, or writes to any file a decrypted secret value. `std::printf`
  is used once, for the vault *path*, not its contents — confirm that
  stays true if you add logging anywhere while reviewing.

### 4.6 The IPC transport — `libs/ipc/src/pipe.{hpp,cpp}`

- `PipeServer`/`PipeClient`/`PipeConnection`, and specifically the SDDL
  descriptor (`"D:(A;;GA;;;OW)(A;;GA;;;SY)"` — owner + SYSTEM only) passed
  to `CreateNamedPipeA`.
- **Check**: this SDDL string is the actual access-control boundary
  preventing another local user account from connecting to the pipe.
  Confirm it parses and applies successfully (`PipeSecurity`'s
  constructor) and that failure to build the descriptor doesn't silently
  fall through to Windows' default (more permissive) DACL without at
  least being a deliberate, documented choice — read the fallback comment
  and decide whether you agree with it.
- **Check**: message framing (`send`/`receive`'s length-prefix protocol) —
  confirm a malformed or truncated length prefix from an untrusted peer
  (in principle, anything with a handle to the pipe before the DACL is
  applied, or a bug on either end) can't cause an unbounded read/
  allocation.
- Out of scope by the threat model's own framing: this transport is
  local-machine-only by construction (no network listener anywhere) —
  you don't need to think about network attackers here.

### 4.7 The desktop-side client — `apps/desktop/src/VaultClient.{hpp,cpp}`

- **Check**: this is the one place outside `apps/vault` that ever sees a
  decrypted value (for display/copy). Confirm it never persists one —
  no logging, no crash-dump-visible retention beyond what's needed to
  render it once, and that the clipboard-clear timeout (`MainWindow`'s
  vault page, 30s) actually clears the clipboard's *content*, not just a
  UI flag (i.e. confirm it doesn't leave the real secret sitting in the
  OS clipboard after the visible countdown ends if the app was closed
  mid-countdown).

## 5. What's already been checked (don't duplicate, but do spot-check)

From `docs/security/vault-threat-model.md`'s review checklist:

- KDF parameters (`KdfParams::interactive()`) compared against current
  OWASP Argon2id guidance.
- The file parser fuzzed (3000 rounds of randomized mutation against
  `read_header()`, 150 against `unlock()`) — found and fixed the
  opslimit/memlimit bound issue described in 4.2 above.
- `sodium_mlock` confirmed to actually succeed on this project's Windows
  dev machine (10.0.19045), not just assumed from documentation.

These were done by the vault's own author, which is exactly why this
independent pass matters — a second reader is far more likely to notice
a design blind spot than someone re-checking their own reasoning.

## 6. Explicitly out of scope for this review

Per the threat model (re-stated here so you don't spend time on it):
remote/network attackers (no network-facing API exists), a compromised OS
kernel or an attacker with admin/SYSTEM rights on the same machine (game
over for any in-memory protection at that point), physical/hardware
attacks (cold boot, DMA) and side-channel timing analysis of the
primitive implementations themselves, supply-chain compromise of
libsodium, and multi-user/shared-machine isolation beyond normal OS file
permissions.

## 7. Reporting findings

For each finding, note:

- **Where**: file and line (or verb name / message flow).
- **What**: the specific behavior you observed, not just "this looks
  risky" — ideally with a reproduction (a sequence of verbs, a crafted
  file, a specific input).
- **Why it matters**: which of the threat model's adversaries (section
  "Adversaries considered") this actually affects, if any — a finding
  that only matters under an explicitly-out-of-scope adversary is still
  worth recording, just flagged as such rather than urgent.
- **Severity**, in your own judgment: something that lets another local
  *unprivileged* process or user read decrypted vault data is critical;
  something that only degrades under an already-out-of-scope adversary
  (e.g. admin/SYSTEM) is informational.

File findings as an issue, a PR comment on the relevant lines, or a
written report — whatever's easiest for you; there's no existing template
to conform to. Once this pass is done, check off the last item in
`docs/security/vault-threat-model.md`'s review checklist and record who
did it and when, the same way the other three items there are dated and
attributed to their own review pass.
