# Vault - Windows App/Game Locker

## Overview

Vault is a production-quality Windows desktop application that detects installed games and applications, lets the user lock individual ones, and enforces that lock by suspending the process at the kernel level until the user authenticates via Windows Hello (face/fingerprint), a 4-digit PIN, or a password.

## Architecture

```
Vault/
├── VaultService/              (Windows Service, C++)
│   ├── SuspendEngine          - NtSuspendProcess / NtResumeProcess wrapper
│   ├── DetectionEngine        - Steam manifest parsing, registry scan, .lnk resolution, icon extraction
│   ├── ConfigStore            - Encrypted (DPAPI) JSON store: watch-list, locked state, hashed PIN/password, exclusion list, session logs
│   ├── ProcessWatcher         - Polls process snapshots, detects locked-app launches
│   ├── IPCServer              - Named pipe server for UI communication
│   └── main.cpp               - Service entry point, install/uninstall handlers
│
├── VaultUI/                   (WebView2 host, C++)
│   ├── WebViewHost            - Creates WebView2 environment/controller
│   ├── IPCClient              - Named pipe client, talks to VaultService
│   ├── main.cpp               - UI entry point
│   └── ui/
│       └── vault_ui_v2.html   - Liquid-glass frontend
│
└── Shared/
    ├── IPCProtocol.h          - Message schema shared by service and UI
    └── Types.h                - Common types and constants
```

## Building

### Prerequisites

1. **Visual Studio 2022** with C++ Desktop workload
2. **Windows SDK** 10.0.19041.0 or later
3. **WebView2 SDK** - Install via NuGet or download from Microsoft
4. **WIL (Windows Implementation Libraries)** - Available via NuGet or GitHub

### Build Steps

```batch
# Using Visual Studio Developer Command Prompt
cd Vault

# Build VaultService
cl.exe /EHsc /std:c++17 /I Shared /I "%WindowsSdkDir%Include\%WindowsSDKVersion%\um" ^
     /I "%WindowsSdkDir%Include\%WindowsSDKVersion%\shared" ^
     VaultService/*.cpp /link /OUT:VaultService.exe ^
     kernel32.lib user32.lib shell32.lib advapi32.lib crypt32.lib bcrypt.lib ole32.lib

# Build VaultUI (requires WebView2 SDK)
cl.exe /EHsc /std:c++17 /I Shared /I "%WindowsSdkDir%Include\%WindowsSDKVersion%\um" ^
     /I "%WindowsSdkDir%Include\%WindowsSDKVersion%\shared" ^
     /I "packages\Microsoft.Web.WebView2.*/build/native/include" ^
     VaultUI/*.cpp /link /OUT:VaultUI.exe ^
     kernel32.lib user32.lib shell32.lib ole32.lib oleaut32.lib comctl32.lib ^
     "packages\Microsoft.Web.WebView2.*/build/native/x64/WebView2LoaderStatic.lib"
```

### CMake Alternative

A CMakeLists.txt can be created for easier cross-compilation:

```cmake
cmake_minimum_required(VERSION 3.20)
project(Vault VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vault Service
add_executable(VaultService
    VaultService/main.cpp
    VaultService/SuspendEngine.cpp
    VaultService/DetectionEngine.cpp
    VaultService/ConfigStore.cpp
    VaultService/ProcessWatcher.cpp
    VaultService/IPCServer.cpp
)

target_include_directories(VaultService PRIVATE Shared)
target_link_libraries(VaultService PRIVATE 
    kernel32 user32 shell32 advapi32 crypt32 bcrypt ole32)

# Vault UI
add_executable(VaultUI
    VaultUI/main.cpp
    VaultUI/IPCClient.cpp
    # WebViewHost.cpp - requires WebView2 SDK
)

target_include_directories(VaultUI PRIVATE Shared)
target_link_libraries(VaultUI PRIVATE 
    kernel32 user32 shell32 ole32 oleaut32 comctl32)
```

## Installation

### Install the Service

Run as Administrator:

```batch
VaultService.exe install
```

This registers the service with Windows SCM for automatic start.

### Uninstall the Service

```batch
VaultService.exe uninstall
```

### Running in Debug Mode

```batch
# Run service in console mode (for debugging)
VaultService.exe run

# Run UI separately
VaultUI.exe
```

## Features

### Detection Sources (Priority Order)

1. **Steam Library** - Parses `libraryfolders.vdf` and `appmanifest_*.acf` files
2. **Registry Uninstall Keys** - `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`
3. **Start Menu Shortcuts** - Resolves `.lnk` files via `IShellLink`
4. **Running Processes** - `CreateToolhelp32Snapshot` with `TH32CS_SNAPPROCESS`

### Exclusion List

Default exclusions (stored in config, editable by user):
- `node.exe`, `python.exe`, `python3.exe`, `pythonw.exe`
- `cmd.exe`, `powershell.exe`, `pwsh.exe`
- `explorer.exe`, `dwm.exe`, `svchost.exe`
- `vault.exe`, `vaultservice.exe`
- Any process from `C:\Windows\System32` or `C:\Windows\SysWOW64`

### Authentication Methods

1. **PIN** - 4-digit, PBKDF2-hashed with BCrypt
2. **Password** - Optional, same hashing approach
3. **Face ID** - Requires WinRT `UserConsentVerifier` integration (flagged for v2)
4. **Touch ID** - Same as Face ID (flagged for v2)

### Security

- Credentials stored as salted PBKDF2 hashes (100,000 iterations)
- Config file encrypted with Windows DPAPI
- No plaintext credential storage

## Known Limitations (v1)

### Icon Extraction
The `ConvertHICONtoPNG` function in `DetectionEngine.cpp` is a stub. For production use, integrate:
- **Windows Imaging Component (WIC)** for PNG encoding
- Or **libpng** library

### Windows Hello Integration
Face ID and Touch ID require WinRT `Windows.Security.Credentials.UI.UserConsentVerifier` API. This needs:
- C++/WinRT headers
- Application manifest capabilities
- Proper async/await handling

Marked as "not available" in current build; UI correctly shows these methods as unavailable.

### Process Detection Latency
Uses polling via `CreateToolhelp32Snapshot` at ~300ms intervals. A kernel driver using `PsSetCreateProcessNotifyRoutine` would provide instant notification but requires:
- Signed driver (EV certificate)
- Additional development scope

## IPC Protocol

Messages are length-prefixed JSON over named pipe `\\.\pipe\VaultIPC`:

```
[4-byte little-endian length][JSON payload]
```

Message types defined in `Shared/IPCProtocol.h`.

## Testing Checklist

- [ ] Launch VaultUI → shows detected apps with icons
- [ ] Lock an app (e.g., Notepad)
- [ ] Launch the locked app → auth overlay appears
- [ ] Enter correct PIN → app resumes
- [ ] Activity drawer shows real session stats
- [ ] Excluded processes never appear in list

## License

MIT License - See LICENSE file
