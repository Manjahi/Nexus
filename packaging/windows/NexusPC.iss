; NexusPC installer (Inno Setup 6). Milestone 8 packaging.
;
; Build the app first (Release config, with Qt), then compile this script:
;   cmake --preset windows-msvc
;   cmake --build --preset release
;   ISCC packaging\windows\NexusPC.iss
;
; Output lands in build\installer\NexusPC-Setup-<version>.exe. Override the
; source directory (e.g. to package a different build tree) with:
;   ISCC /DSourceDir="C:\path\to\Release" packaging\windows\NexusPC.iss

#define MyAppName "NexusPC"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "NexusPC"
#define MyAppExeName "NexusPC.exe"
#define MyAppURL "https://github.com/your-org/nexuspc"

#ifndef SourceDir
  #define SourceDir "..\..\build\windows-msvc\apps\desktop\Release"
#endif

[Setup]
; Fixed AppId - do not change across releases, or Windows will treat every
; version as a different, side-by-side-installable application instead of
; letting the installer upgrade in place.
AppId={{BB99E189-D621-47E9-BFD8-E1632807DAC7}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Let the user choose (via a dialog) whether to install for just themselves
; (no admin needed) or for all users (needs elevation) - UFR-015's "minimum
; permissions needed" spirit extended to the installer itself.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\build\installer
OutputBaseFilename=NexusPC-Setup-{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
; No code-signing certificate available for this project yet - Windows
; SmartScreen will warn on first run of an unsigned installer/exe. See
; packaging/windows/README.md's "Code signing" section for how to obtain one
; (SignPath Foundation's free OSS program, or Azure Artifact Signing) and the
; exact command to sign with it once you have it.
;
; This whole block is compiled in only when the caller passes /DSignRelease
; (see the README) - by default it's entirely absent, so every build this
; project has always produced (unsigned) is unaffected. When it IS defined,
; the caller must also pass a matching /S"release=..." sign-tool definition
; (ISCC's own mechanism for the actual signtool.exe command) - see the
; README for the exact invocation.
#ifdef SignRelease
SignTool=release
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Everything windeployqt + the CMake post-build step already staged next to
; NexusPC.exe: Qt runtime, plugin subfolders, nexuspc-vault.exe + its DLLs,
; and every other third-party DLL the app links. Build artifacts (.pdb/.ilk/
; import libs) aren't needed by an end user, so they're excluded.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion; Excludes: "*.pdb,*.ilk,*.exp,*.lib"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; The vault runs as its own process (ADR-0003) and may still be alive and
; holding its files open if the user unlocked it during this session -
; stop it before the uninstaller tries to remove its files.
Filename: "{cmd}"; Parameters: "/C taskkill /IM nexuspc-vault.exe /F"; Flags: runhidden skipifdoesntexist; RunOnceId: "KillVault"

[UninstallDelete]
; The app creates its database/vault/reports under %APPDATA%\NexusPC at
; runtime (never touched by the installer) - leave user data in place on
; uninstall by default; nothing to add here. (Revisit if a "remove my data
; too" option is ever wanted - it would target {userappdata}\NexusPC.)
