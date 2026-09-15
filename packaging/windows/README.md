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
  SmartScreen will warn on first run of the unsigned installer/exe. If a
  cert is ever obtained, sign via Inno Setup's `SignTool=` setting.

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
