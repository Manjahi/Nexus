# Releasing NexusPC

How to cut a new installer release. Building the installer itself is
covered in `packaging/windows/README.md` - this document is everything
around that: versioning, tagging, publishing, and every place the
version number has to be updated by hand.

## Places the version number lives (keep them in sync)

There's no single source of truth yet - bumping a release means editing
all of these:

| File | What to change |
|---|---|
| `CMakeLists.txt` | `project(NexusPC VERSION X.Y.Z ...)` |
| `packaging/windows/NexusPC.iss` | `#define MyAppVersion "X.Y.Z"` |
| `docs/index.html` | The download link in the hero CTA, the download button in Get Started, and the version text in the hero note |
| `CHANGELOG.md` | New `[X.Y.Z]` entry |

## Steps

1. **Bump the version** in `CMakeLists.txt` and `NexusPC.iss`. Commit
   that alone first - it's the only step that touches source, and it's
   worth being able to point at in isolation later.
2. **Build Release and test.**
   ```powershell
   cmake --preset windows-msvc
   cmake --build --preset release
   ctest --preset release
   ```
3. **Package the installer** - see `packaging/windows/README.md`'s
   "Build" section. Output lands at
   `build\installer\NexusPC-Setup-<version>.exe`.
4. **Verify it** - the silent install/uninstall check in
   `packaging/windows/README.md`'s "Verifying a build" section. This is
   the only step that exercises the packaged output itself rather than
   the raw build tree; don't skip it because the build succeeded.
5. **Compute a checksum** for the release notes:
   ```powershell
   certutil -hashfile build\installer\NexusPC-Setup-<version>.exe SHA256
   ```
6. **Update `CHANGELOG.md`** with the new version's entry.
7. **Tag and publish the GitHub Release**
   (`github.com/Manjahi/Nexus/releases/new`):
   - Tag: `vX.Y.Z`, target `main`, "Create new tag on publish"
   - Title: `NexusPC X.Y.Z`
   - Description: the CHANGELOG entry for this version, plus the
     SHA-256 and a one-line reminder that the build is unsigned
   - Attach `NexusPC-Setup-X.Y.Z.exe` **with that exact filename** - the
     homepage links to a fixed URL built from it:
     `.../releases/download/vX.Y.Z/NexusPC-Setup-X.Y.Z.exe`
   - Publish
8. **Update the homepage's download links** in `docs/index.html` (see
   the table above) and push. If a copy of the homepage is also
   published elsewhere (e.g. a Claude Artifact), republish that too so
   both stay in sync.

## Notes

- There's no `gh` CLI available in the environment this was written
  from, so step 7 is a manual web-UI step. If `gh` (or CI) becomes
  available later, steps 2-3 and 7 could run headless from a release
  workflow instead - not worth setting up until releases are frequent
  enough to justify it.
- Code signing isn't wired into this flow yet because there's no
  certificate - see the "Code signing" section of
  `packaging/windows/README.md` for what changes once one exists
  (`/DSignRelease` on the Inno Setup invocation in step 3).
