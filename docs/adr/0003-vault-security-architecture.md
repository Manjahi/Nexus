# ADR-0003: Vault security architecture

- Status: Accepted
- Date: 2026-09-11
- Context: Milestone 7 (spec section 2 Module B, section 5 process 3, section
  7's shared-database rule, UFR-011). Full analysis in
  `docs/security/vault-threat-model.md`. ADR-0002 already committed to the
  vault being a separate process "required at Milestone 7" — this ADR makes
  the concrete cryptographic and protocol choices.

## Decision

### Storage format
A dedicated encrypted file (`vault.nxv`), never a table in the shared
`nexuspc.db`. Layout:

- A small plaintext header: magic bytes, format version, Argon2id parameters
  (opslimit/memlimit), and a random salt. Nothing in the header reveals
  vault contents; it only tells the reader how to derive the key and which
  format version follows.
- One encrypted payload: the header (as associated data) plus a serialized
  entry list, sealed with XChaCha20-Poly1305 (`crypto_aead_xchacha20poly1305_ietf`)
  under a key derived from the master password via Argon2id
  (`crypto_pwhash` with `OPSLIMIT_INTERACTIVE`/`MEMLIMIT_INTERACTIVE` as a
  starting point — tunable, and the parameters actually used are recorded in
  the header so future stronger defaults don't break old vaults).
- A random 24-byte nonce per encryption. The whole vault is re-sealed as one
  blob on every save (entries lists are small; this avoids nonce-reuse
  bookkeeping across incremental field updates).

### Process split
`nexuspc-vault` is a separate executable from `nexuspc-ui`. It owns the vault
file exclusively, derives the key and holds decrypted entries only in its own
memory, and exposes a minimal request/response API over a local Windows named
pipe (`\\.\pipe\nexuspc-vault-<session>`). `nexuspc-ui` spawns it on first use
of the Vault page and talks to it as a client; it never opens or parses
`vault.nxv` itself.

IPC verbs (all synchronous request/response, one connection per `nexuspc-ui`
instance):

| Verb | Purpose |
|---|---|
| `status` | locked/unlocked, entry count, no secret material |
| `unlock` | master password in, ok/error out |
| `lock` | zero in-memory state immediately |
| `list` | entry ids + labels/usernames/tags (no passwords/note bodies) |
| `get` | one entry's full decrypted fields, by id |
| `put` | create or update an entry |
| `delete` | remove an entry by id |
| `generate_password` | stateless password generator (no vault access needed) |
| `health` | weak/reused/old-entry report (ids + reasons, not plaintext) |
| `shutdown` | lock immediately and exit the process - sent by `nexuspc-ui` on normal close so `nexuspc-vault` doesn't outlive it |

`list` and `health` deliberately withhold secret fields so the common "browse
your vault" UI flow never pulls plaintext across the pipe until the user asks
to reveal or copy one specific entry via `get`.

### In-memory hygiene
Inside `nexuspc-vault`: decrypted entries and the derived key live in
`sodium_malloc`'d regions, `sodium_mlock`'d, and `sodium_memzero`'d as soon as
they're replaced or the vault locks. `nexuspc-ui` never stores a decrypted
value beyond the single display/clipboard use that triggered the `get` call.

### Auto-lock and clipboard timeout
`nexuspc-vault` runs its own inactivity timer (default 5 minutes, user
configurable) and locks independently of what `nexuspc-ui` does or whether it
has crashed. Clipboard copies of a secret are cleared by `nexuspc-ui` after a
short timeout (default 30 seconds) by overwriting the clipboard, matching the
spec's "Clipboard timeout" requirement.

### Backups
Encrypted vault backups are just timestamped copies of `vault.nxv` — no
separate backup format, since the file is already fully encrypted at rest.
They are explicitly excluded from the general Backup & Recovery module's
content-addressed store (spec section 7 rule: no vault data in shared
infrastructure) and are instead written by `nexuspc-vault` itself, under the
vault's own directory.

## Alternatives considered

- **In-process vault (module like the others), relying on discipline to
  never expose decrypted records.** Rejected: this is exactly the boundary
  spec section 2 calls out by name, and a single process means a bug or
  memory-disclosure vulnerability anywhere in NexusPC (e.g. a parser bug in
  Search or Storage) can potentially read vault secrets. Not acceptable for
  a component whose entire purpose is holding secrets.
- **SQLite with SQLCipher for the vault file**, keeping one query interface
  shape across the app. Rejected: pulls in a second SQLite build variant,
  and a bespoke small-file format is simpler to review line-by-line than a
  general-purpose encrypted database engine, which matters more here than
  code reuse.
- **Per-field encryption instead of sealing the whole entry list.** Rejected
  for v1: more nonce-management complexity for a vault size where "reseal
  everything on save" is fast enough to not matter; revisit if vaults grow
  large enough for whole-file resealing to be a UX problem.

## Consequences

- First production use of a local IPC transport in this codebase — adds a
  small `libnexus-ipc` (named-pipe client/server, framed request/response)
  used only by `nexuspc-ui` and `nexuspc-vault`.
- `nexuspc-vault` must be running for the Vault page to do anything; the UI
  spawns it on demand and treats "pipe not there yet" as a normal startup
  race, not an error.
- Every other module stays exactly as simple as before — no vault-awareness
  needed anywhere outside `apps/desktop`'s Vault page and `apps/vault`.
- The threat model's review checklist is not satisfied by this ADR alone;
  see `docs/security/vault-threat-model.md` before treating this as
  production-ready. As of 2026-09-21, three of its four items are closed
  (KDF parameters validated against current guidance, the file parser
  fuzzed, `sodium_mlock` confirmed effective on Windows) - fuzzing the
  parser found and fixed a real gap: `parse_file()` didn't bound the
  header's attacker-controlled `opslimit`/`memlimit` before `unlock()` ran
  the KDF against them, letting a corrupted or malicious vault file force an
  extremely expensive or large-allocation unlock attempt before
  authentication ever ran. The remaining item (an independent read-through
  by someone other than the author) needs a second person and stays open.
