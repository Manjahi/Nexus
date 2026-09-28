# Windows installer

Builds a single-file installer (`NexusPC-Setup-<version>.exe`) with
[Inno Setup 6](https://jrsoftware.org/isinfo.php).

## Prerequisites

```powershell
choco install innosetup -y   # needs an elevated prompt
```

## Build

```powershell
# 1. Build a Release configuration (Debug builds aren't meant for distribution).
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
cmake --preset windows-msvc
cmake --build --preset release

# 2. Compile the installer.
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\windows\NexusPC.iss
```

Output: `build\installer\NexusPC-Setup-<version>.exe`.

To package a different build tree, override the source directory:

```powershell
ISCC /DSourceDir="C:\some\other\Release" packaging\windows\NexusPC.iss
```

## What it does

- Packages `NexusPC.exe`, the Qt runtime + plugins, and `nexuspc-vault.exe`
  with its own dependencies (everything CMake's post-build step and
  `windeployqt` already staged next to `NexusPC.exe` - see
  `apps/desktop/CMakeLists.txt`).
- Installs per-user by default with no admin prompt (`PrivilegesRequired=
  lowest`), but lets the user choose an all-users install instead
  (`PrivilegesRequiredOverridesAllowed=dialog`) - UFR-015's "minimum
  permissions needed" extended to the installer itself.
- Stops `nexuspc-vault.exe` before uninstalling, so an unlocked vault
  process doesn't hold its own files open and block removal.
- Never touches `%APPDATA%\NexusPC` (the database, vault file, and reports
  the app creates at runtime) - uninstalling leaves user data in place.
- No code-signing certificate exists for this project, so Windows
  SmartScreen will warn on first run of the unsigned installer/exe. See
  "Code signing" below for how to get one and how to actually sign with it -
  the script is already wired up and tested for this, just waiting on a
  certificate.

## Code signing

Not signed today - there's no certificate. The project's public
[code signing policy](../../README.md#code-signing-policy) commits to
SignPath Foundation as the intended path (see below); Azure Artifact
Signing is kept here as the fallback if SignPath eligibility doesn't
work out. Researched 2026-09-21 (re-verify current pricing/terms before
committing to either, they change):

- **[SignPath Foundation](https://signpath.io/solutions/open-source-community)**
  signs qualifying open-source projects for free: public repo, OSI-approved
  license (NexusPC is MIT - qualifies), no malware, and a verifiable build
  (SignPath's own CI pipeline builds from source and signs the artifact - the
  private key never touches this machine). Best fit if the extra CI
  integration work is worth it for a $0 ongoing cost.
- **[Azure Artifact Signing](https://azure.microsoft.com/en-us/products/artifact-signing)**
  (formerly "Trusted Signing") is Microsoft's own pay-as-you-go option aimed
  at individual developers: no hardware token needed (the key lives in an
  Azure-managed HSM), identity-verified once, ~$9.99/mo for the Basic tier
  (5,000 signatures) as of mid-2026. Simpler to integrate (it's just another
  `signtool sign` invocation with an Azure-issued cert) but an ongoing cost
  and requires identity verification as a business or self-employed
  individual in a supported region (US/CA/EU/UK as of this writing).

Traditional CA-issued OV/EV Authenticode certs are the other option but, as
of the CA/Browser Forum's June 2023 baseline requirements, the private key
must live on a hardware token or a cloud HSM either way - there's no cheap
"just a .pfx file" option left industry-wide, which is part of why the two
above are the more practical routes for a solo project.

### Signing once you have a certificate

`NexusPC.iss`'s `[Setup]` section only adds `SignTool=`/`SignedUninstaller=yes`
when the compiler is invoked with `/DSignRelease` - normal (unsigned) builds
are completely unaffected. This mechanism was tested end-to-end with a local
throwaway self-signed certificate (confirmed the resulting installer carried
a real Authenticode signature - it just didn't chain to a trusted root,
exactly as expected for a self-signed test cert); wiring a real cert in is
just supplying real values below:

```powershell
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64\signtool.exe"
$pfx = "C:\path\to\your-cert.pfx"      # or an Azure Artifact Signing / SignPath invocation instead
$sArg = 'release=$q' + $signtool + '$q sign /f $q' + $pfx + '$q /p YOUR_PFX_PASSWORD /fd sha256 /tr http://timestamp.digicert.com /td sha256 $f'
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "/DSignRelease" "/S$sArg" packaging\windows\NexusPC.iss
```

`$q`/`$f` are Inno Setup's own tokens (substituted with a literal quote and
the file-to-sign's path respectively) - leave them as the literal text
`$q`/`$f`, don't try to expand them yourself. Add `/tr .../td sha256` (RFC
3161 timestamping, shown above) so the signature stays valid after the
certificate itself expires. Never commit a `.pfx` or its password to the
repository - pass them at build time only (an environment variable or a
local, gitignored file), and prefer a CI secret store over a developer
machine for anything beyond one-off testing.

## Verifying a build

Both directions were exercised end-to-end while building this (not just
compiled and assumed working):

```powershell
# Silent install to a scratch directory, confirm it launches, then uninstall.
$dir = "$env:LOCALAPPDATA\NexusPC_InstallTest"
Start-Process "build\installer\NexusPC-Setup-<version>.exe" `
    -ArgumentList "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/DIR=`"$dir`"" -Wait
Start-Process "$dir\NexusPC.exe"   # confirm it runs, then close it
Start-Process "$dir\unins000.exe" -ArgumentList "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART" -Wait
```
