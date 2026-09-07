# NexusPC coding standards

Keep this short; the tooling enforces most of it.

## Language & build
- C++20, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`). C only where an OS
  API genuinely requires it, isolated in a provider `.cpp`.
- Every target links `nexuspc::warnings`. Warnings are errors in CI.
- No new third-party dependency without an ADR and a `vcpkg.json` entry.

## Layout & dependencies
- Dependency direction is strictly downward: `apps` → `modules` → `app_services`
  → `libs`. Libraries never include from `modules`/`apps`.
- **Modules never include or link each other.** Cross-module cooperation goes
  through `app_services` interfaces (event bus, query services). See spec §9.
- Public headers live under `libs/<name>/include/nexus/<name>/`. Everything else
  is private to the target.

## Naming
- `namespace nexus::<lib>` for libraries, `nexuspc::<area>` for apps/services.
- Types `CamelCase`, functions & variables `lower_case`, private members
  trailing `_`, constants `kCamelCase` or `lower_case` `constexpr`.
- Files: headers `.hpp`, sources `.cpp`; one primary type per file, filename
  matches the type for classes (`MainWindow.hpp`).

## Style
- Format with `.clang-format` (LLVM base, 4-space indent, 100 cols) before every
  commit.
- Prefer `Result<T, E>` (`nexus/core/result.hpp`) over exceptions for expected
  failures; exceptions only at process/API boundaries.
- No raw `new`/`delete`; owning pointers via smart pointers or Qt parent-child.
- `[[nodiscard]]` on functions whose result must be checked.

## Tests
- Catch2 v3. Every library target has a `tests/unit/<lib>/` suite.
- A bug fix lands with a regression test. New public behaviour lands with tests
  in the same change.
- `ctest --preset debug` must be green before pushing.

## Commits
- Conventional-ish: `area: imperative summary` (e.g. `core: add Uuid parse`).
- Keep commits buildable.
