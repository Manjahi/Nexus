# ADR-0002: Process model and platform scope

- Status: Proposed
- Date: 2026-09-07
- Context: NexusPC spec sections 5 (process/security architecture) and 4 (product architecture). The spec describes three processes (`nexuspc-ui`, `nexuspc-agent`, `nexuspc-vault`) as a target end state and explicitly frames it as a "recommended evolution".

## Decision

### Platform scope
Ship **Windows 10/11 only** through the MVP (Milestones 1-6). Design every OS-touching capability behind a provider interface (`libnexus-system`, `libnexus-net`, `libnexus-fs` OS adapters) so Linux and macOS providers can be added later without changing modules or application-services.

### Process model
Start **in-process**: a single `nexuspc-ui` process with all modules linked as libraries, coordinating through the application-services layer (module registry, event bus, alert center, report center, audit log).

Split processes only when a milestone needs it:
- `nexuspc-agent` (separate background service): considered from Milestone 2 if unattended sampling/scheduling while the UI is closed becomes a requirement; otherwise the in-process job system runs while the UI is open.
- `nexuspc-vault` (separate isolated process): **required at Milestone 7**. The Vault is a security boundary (spec section 2, UFR-011); decrypted secret material must not live in the general UI process. This is the first production use of the local IPC layer.

### IPC
- Local IPC only, disabled for remote by default (UFR-012).
- Transport: Windows named pipes behind a small transport-agnostic interface.
- A throwaway named-pipe echo spike is done in Phase 0 to de-risk the layer well before Milestone 7.

## Rationale

- Fastest path to visible results (spec Milestone 1-2 rationale): no IPC plumbing before there is a feature to show.
- The spec itself recommends this staging ("recommended evolution", "build last and treat as a separate security project").
- The cost of deferring is that the IPC layer is unproven until late — mitigated by the Phase 0 spike and by keeping the vault API surface minimal.

## Consequences

- Modules must not assume a shared address space in their public contracts: cross-module communication goes through application-services interfaces (event bus + query interfaces), never direct calls. This keeps a later process split cheap.
- `apps/agent` and `apps/vault` exist as build targets from Phase 0 but stay as stubs until their milestone.
- Crash isolation (UFR-020) is enforced in-process at the application-services boundary (catch, mark module degraded) until/unless a module moves to its own process.
