# Third-party notices

NexusPC is MIT-licensed (see `LICENSE`). It builds on the open-source
components below.

## Bundled assets

### Fluent UI System Icons

Sidebar navigation icons (`Media/icons/*.svg`, except `continuity.svg` -
see below) are sourced from Microsoft's **Fluent UI System Icons**
(https://github.com/microsoft/fluentui-system-icons), "Rounded Regular"
style at 24px, unmodified except for recoloring at load time (see
`apps/desktop/src/Theme.cpp`'s icon-loading helper, which rewrites the
source SVG's fill/stroke color to the NexusPC palette token appropriate
for each navigation state - normal, selected, disabled).

```
MIT License

Copyright (c) Microsoft Corporation.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

`Media/icons/continuity.svg` (the Continuity module's icon) is an
original NexusPC asset, hand-authored to visually match the Fluent
family's weight and geometry (no "infinity"/continuity glyph exists in
Microsoft's set) - not subject to the above license.

See `Media/icons/README.md` for the icon-to-module mapping.

## Build dependencies (vcpkg)

Every dependency below is fetched and built by vcpkg (see `vcpkg.json`),
which stages each package's own license/copyright file under
`<build-dir>/vcpkg_installed/x64-windows/share/<package>/copyright` -
that staged file is the authoritative license text for the exact version
in use; this table is a pointer to it, not a restatement.

| Dependency | Used for | License family |
|---|---|---|
| `libsodium` | Vault crypto (Argon2id, XChaCha20-Poly1305) | ISC |
| `blake3` | Content hashing (dedup, backup) | CC0-1.0 / Apache-2.0 (dual) |
| `sqlite3` | The shared database | Public domain |
| `curl` | HTTP probes, speed test | curl license (MIT/ISC-style) |
| `nlohmann-json` | JSON (vault wire protocol, reports, settings) | MIT |
| `spdlog` | Logging | MIT |
| `catch2` | Test framework (dev-only, not shipped) | BSL-1.0 |
| `libzip` | `.docx` extraction (Search) | BSD-3-Clause |
| `pugixml` | `.docx` XML parsing (Search) | MIT |

Qt 6 (Widgets, Charts) is used under LGPL-3.0. NexusPC links it
dynamically (Qt ships as separate DLLs next to `NexusPC.exe`, staged by
`windeployqt` - see `packaging/windows/README.md` - never statically
linked), which is what LGPL-3.0 requires for a proprietary or
differently-licensed application to link against it without inheriting
its terms; NexusPC's own license is MIT regardless.
