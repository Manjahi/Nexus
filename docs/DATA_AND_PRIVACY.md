# Data and privacy

The short version: NexusPC doesn't send anything anywhere. Everything it
collects, it collects because you asked it to do something (scan a folder,
watch a device, back up a directory), and everything it stores stays on
your computer, in one folder, in formats you can inspect or delete yourself.

## Nothing leaves your computer

- No telemetry, no analytics, no crash reporting to a server, no update
  checker phoning home.
- No network listener of any kind - NexusPC cannot be reached from another
  computer, and nothing about it is remotely manageable. The only network
  traffic NexusPC ever generates is traffic *you* asked for: pinging a
  device in a range you typed in, checking whether a target you configured
  is reachable, running a backup to a folder you chose.
- Network device discovery never scans anything on its own initiative. It
  only ever walks a CIDR range you explicitly typed into the Network page
  yourself - see `docs/UFR_CONFORMANCE.md` for exactly how that's enforced
  in code.

## Where things live

Everything is under `%APPDATA%\NexusPC` on your machine:

| What | Where | Format |
|---|---|---|
| Everything except the vault (hardware/network/connectivity history, storage scan results, backup job metadata, search index, notifications, audit trail, settings) | `%APPDATA%\NexusPC\nexuspc.db` | One SQLite database |
| Your vault (passwords, secure notes) | `%APPDATA%\NexusPC\vault.nxv` | A separate, independently encrypted file - **never** stored in the database above. See "The vault" below. |
| Backup snapshot contents | wherever you pointed a backup job's destination | Content-addressed blobs, keyed by BLAKE3 hash |
| Generated reports | `%APPDATA%\NexusPC\reports\` | HTML/CSV files, written when you click "Generate" |

Uninstalling NexusPC never touches this folder - your data survives an
uninstall/reinstall/upgrade. To remove it yourself, just delete
`%APPDATA%\NexusPC`.

## What each module actually stores

- **Storage**: which folders you scanned, and which files it found were
  exact duplicates of each other (file paths and hashes - never file
  contents).
- **Vault**: whatever entries you create - titles, usernames, passwords,
  URLs, notes, tags - encrypted at rest. See the next section.
- **Network**: the CIDR ranges you've added, and the devices that answered
  when you scanned them (IP address, hostname if resolvable, an optional
  label you give it, and a ping history).
- **Internet/Connectivity**: reachability and latency history for a small,
  fixed set of well-known targets (DNS resolvers, a captive-portal check),
  used to distinguish "the internet is down" from "your PC can't reach the
  router."
- **Performance**: periodic CPU/memory/process samples, sampled from the
  same OS APIs Task Manager uses.
- **Backup**: which files you backed up, when, and their content hashes -
  plus the actual file content, in the destination you chose.
- **Search**: the text content of files you explicitly indexed (so they can
  be searched), extracted from source/markup/config files - never binaries.

## How long data is kept

Hardware, connectivity, and network history are pruned automatically -
each on its own configurable schedule (Settings → Data retention), 7-30
days by default. Backup snapshots are pruned to however many you configure
per job ("keep the last N"). Deleting a storage scan, a network range, a
vault entry, or a backup job is immediate and explicit - you do it, it's
gone from the database.

Some categories don't yet clean themselves up automatically: search index
entries, notification history, job run history, and generated report files
currently accumulate until you clear them yourself (delete `nexuspc.db` to
reset everything, or delete individual report files from the `reports`
folder). This is an active area of ongoing hardening, not a design
decision - the technical detail and status is tracked in
`docs/UFR_CONFORMANCE.md`'s "Known gaps" section.

## The vault

The vault is treated as its own security boundary, deliberately isolated
from the rest of NexusPC:

- It runs as a separate process (`nexuspc-vault`), not inside the main app.
  No other module - and no bug in any other module - has a code path that
  can reach a decrypted vault entry.
- It's a standalone encrypted file, never a table in the shared database.
  Unlocking it derives a key from your master password (Argon2id) and
  decrypts with XChaCha20-Poly1305; NexusPC never sees your master password
  again after that, and never stores it anywhere.
- It locks itself automatically after 5 minutes idle, independent of
  whether the rest of the app is even open.
- Copying a password to the clipboard clears it again after 30 seconds.

Full design and threat-model writeup, including what's explicitly out of
scope: `docs/security/vault-threat-model.md` and
`docs/adr/0003-vault-security-architecture.md`.

## Honesty about what isn't reviewed yet

This project doesn't have a code-signing certificate, so Windows will warn
on first run of the installer or the app itself - that's a cost of not
having a cert yet, not a sign of something wrong. It also hasn't had an
independent third-party security review; the threat model document above
has its own checklist of what that review should cover before treating the
vault as fully production-hardened.
