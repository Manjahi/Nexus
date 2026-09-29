# Security policy

## Reporting a vulnerability

If you find a security vulnerability in NexusPC, please report it
privately through GitHub's
[Security Advisories](https://github.com/Manjahi/Nexus/security/advisories/new)
rather than opening a public issue - that keeps the report private until
a fix is ready.

Include what you'd include in any report: the affected version, steps to
reproduce, and what you'd expect to happen instead. For a vault-related
finding, the design it's checked against is written up in
[`docs/security/vault-threat-model.md`](docs/security/vault-threat-model.md)
and [ADR-0003](docs/adr/0003-vault-security-architecture.md) - worth a
look first in case it's already a documented, out-of-scope tradeoff
rather than a bug.

This is a solo/small project without a dedicated security team, so
there's no guaranteed response time yet - reports will be looked at as
soon as possible.

## Supported versions

Pre-1.0: only the most recent release is supported. There isn't a
long-term support branch yet.

## Known, already-documented gaps

These aren't secret and don't need a private report - they're tracked
openly:

- **No code-signing certificate** - Windows SmartScreen warns on first
  run of the installer and the app. See the
  [code signing policy](README.md#code-signing-policy).
- **No independent third-party security review of the vault yet** - see
  the "Honesty about what isn't reviewed yet" section of
  [`docs/DATA_AND_PRIVACY.md`](docs/DATA_AND_PRIVACY.md).

Both are being worked on, not overlooked.
