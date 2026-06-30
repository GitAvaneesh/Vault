# Build Vault — a native Windows app/game locker (C++)

You are building **Vault**, a production-quality Windows desktop application that detects installed games and applications, lets the user lock individual ones, and enforces that lock by suspending the process at the kernel level until the user authenticates via Windows Hello (face/fingerprint), a 4-digit PIN, or a password. The app also logs session activity (login time, duration, logout time, failed attempts) per locked app.

**Hard rule: no fake placeholders, no stub functions that "pretend" to work, no hardcoded mock data standing in for real detection/auth/logging.** Every feature listed below must be a real, working implementation against real Windows APIs. If something genuinely cannot be implemented (e.g. requires a signed kernel driver you don't want to ship in v1), say so explicitly in the code comments and in your response — do not silently fake it with a sleep() and a hardcoded "success".

---

## 1. What the app does (functional spec)

1. **Detection** — On launch and periodically thereafter, Vault scans the system for installed games and applications and displays them in a grid with their real extracted icons. Detection sources, in priority order:
   - Steam library: parse `libraryfolders.vdf` and per-game `appmanifest_*.acf` files to get installed Steam games, their install directories, and executable names.
   - Installed Programs: enumerate `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall` and the WOW6432Node equivalent for non-Steam installed software.
   - Start Menu shortcuts: resolve `.lnk` files in the user and all-users Start Menu folders via `IShellLink`/`IPersistFile` to get target executables.
   - Running processes: `CreateToolhelp32Snapshot` with `TH32CS_SNAPPROCESS` to catch anything currently running that wasn't caught above.
   - Deduplicate by resolved executable path.

2. **Exclusion list** — The following are NEVER shown as lockable, regardless of how they were detected: `node.exe`, `python.exe`, `python3.exe`, `pythonw.exe`, `cmd.exe`, `powershell.exe`, `pwsh.exe`, `explorer.exe`, `dwm.exe`, `svchost.exe`, `vault.exe`/`vaultservice.exe` (itself), and any process running from `C:\Windows\System32` or `C:\Windows\SysWOW64` unless the user explicitly adds it. This list must be stored in config, not hardcoded only in source, so the user can edit it later.

3. **Icons** — Extract the real icon for each detected executable via `SHGetFileInfo` (`SHGFI_ICON | SHGFI_LARGEICON`) or `ExtractIconEx`, convert the `HICON` to a PNG (32-bit RGBA), and cache it to disk (`%LOCALAPPDATA%\Vault\icons\<hash>.png`) so the UI can load it as a file path or data URI. No emoji or placeholder icons in the real app — every card shows the actual extracted icon.

4. **Locking** — The user can toggle "locked" per detected app/game from the UI. Locked state is persisted (see Config Store below).

5. **Enforcement** — A background service watches for process creation of any locked executable. The moment a locked process is created:
   - Suspend it immediately using `NtSuspendProcess` (via `ntdll.dll`, dynamically resolved with `GetProcAddress` since it's undocumented) before it can render a window or do meaningful work. Use polling via `CreateToolhelp32Snapshot` on a short interval (200–400ms) as the v1 detection mechanism for new process creation; note in comments that a `PsSetCreateProcessNotifyRoutine` kernel driver would catch this instantly but requires a signed driver and is out of scope for v1.
   - Signal the UI host (via the IPC channel, see Architecture) to show the auth overlay for that specific app.
   - On successful auth: `NtResumeProcess` to let it continue normally, log a session-start event.
   - On the auth overlay being cancelled/dismissed without success: kill the suspended process (`TerminateProcess`) rather than leaving it suspended forever.
   - On the process naturally exiting (whether or not it was ever unlocked that session): log a session-end event with duration.

6. **Authentication — 4 methods, exactly as specified:**
   - **Face ID** — call Windows Hello via `Windows.Security.Credentials.UI.UserConsentVerifier` (WinRT) or the `WinBio` API for facial recognition if the device has compatible hardware. Real call, real result (`UserConsentVerificationResult`), not a timer that always succeeds.
   - **Touch ID (fingerprint)** — same `UserConsentVerifier` API, which on Windows uses whatever Hello-enrolled modality is available (fingerprint reader). If the device has no fingerprint reader, the UI must reflect that ("Touch ID is not available on this device") rather than faking success.
   - **PIN** — 4-digit, set by the user in onboarding/settings. Stored only as a salted hash (PBKDF2 or BCrypt via Windows CNG, never plaintext) in the encrypted config store. Verify by hashing the entered PIN with the stored salt and comparing.
   - **Password** — same hashing approach as PIN, separate field, optional. If the user has never set a password, the UI must show "You haven't set this up" exactly as designed in the prototype, and that method must be disabled/unselectable until set.
   - Auth attempts (success and failure, and which method was used) must be logged.

7. **Session stats (per locked app)** — Vault must track and expose, per app:
   - Timestamp of each successful unlock (login)
   - Duration the process ran unlocked before it exited or was re-locked
   - Timestamp of last exit (logout)
   - Count of failed auth attempts, with timestamps and which method failed
   This data must be real, derived from actual suspend/resume/process-exit events — not sample/mock data.

8. **UI** — WebView2-hosted frontend (HTML/CSS/JS) matching the liquid-glass design already built (reference file: `vault_ui_v2.html`, attached/provided separately — port its markup, CSS, and interaction logic faithfully, do not redesign it). Screens: dashboard grid (Games/Applications sections), auth overlay (4-method picker → PIN keypad / password field / Face ID scan / Touch ID scan → success), Activity drawer (real stats per app), Settings (manage PIN/password, exclusion list, autostart toggle).

---

## 2. Architecture

```
Vault/
├── VaultService/              (Windows Service, C++)
│   ├── ProcessWatcher          - polls process snapshots, detects locked-app launches
│   ├── SuspendEngine            - NtSuspendProcess / NtResumeProcess wrapper
│   ├── DetectionEngine          - Steam manifest parsing, registry scan, .lnk resolution, icon extraction
│   ├── ConfigStore               - encrypted (DPAPI) JSON store: watch-list, locked state, hashed PIN/password, exclusion list, session logs
│   ├── AuthBridge                 - exposes a local named-pipe IPC server for the UI host to talk to
│   └── main.cpp                    - service entry point, install/uninstall (SCM) handlers
│
├── VaultUI/                    (separate process, WebView2 host, C++)
│   ├── WebViewHost              - creates the WebView2 environment/controller, loads the HTML/CSS/JS UI
│   ├── IPCClient                  - named-pipe client, talks to VaultService
│   ├── NativeBridge                - exposes C++ functions to JS via WebView2's postMessage/AddHostObjectToScript (icon paths, auth triggers, Hello calls)
│   └── ui/                          - the actual HTML/CSS/JS (ported from the liquid-glass prototype)
│
└── Shared/
    ├── IPCProtocol.h            - message schema shared by service and UI (use a simple length-prefixed JSON or a binary struct protocol, your choice, but document it)
    └── Types.h                    - AppEntry, AuthMethod, SessionLogEntry, etc.
```

**IPC**: use a named pipe (`\\.\pipe\VaultIPC`). Define a clear message protocol (e.g. JSON lines or a tagged binary struct) for: `RequestAppList`, `AppList` (response), `ToggleLock(appId)`, `LockedAppLaunched(appId, pid)` (service → UI, triggers overlay), `AuthAttempt(appId, method, payload)`, `AuthResult(success, reason)`, `RequestStats(appId)`, `StatsResult(...)`. Document the protocol in `IPCProtocol.h` with real struct/message definitions, not TODOs.

**Why two processes**: the service needs to run elevated/as SYSTEM and survive logoff to keep enforcing locks; the UI needs a user-session WebView2 host, which cannot run as a SYSTEM service. They must stay in sync over IPC.

**Autostart**: register the service for automatic start via SCM (`SERVICE_AUTO_START`) at install time. The UI host can autostart via a Startup-folder shortcut or registry Run key, user-toggleable in Settings.

---

## 3. Security & scope boundaries (read carefully)

- This is a **legitimate personal app-locker**, similar in spirit to commercial parental-control or app-lock software. The background service should be quiet (no taskbar icon, minimal UI footprint) but must NOT actively evade Task Manager, hide its process from process listings, hook other processes' memory, or attempt anti-antivirus/anti-detection techniques. If asked to add such things, refuse and explain why — that crosses into rootkit/malware territory regardless of stated intent.
- All credential material (PIN, password) must be hashed with a proper KDF (BCrypt/CNG `BCryptDeriveKeyPBKDF2` or similar) plus a random salt, stored via DPAPI-encrypted file. Never plaintext, never a "reversible" XOR or similar fake encryption.
- Windows Hello calls must use the real `UserConsentVerifier` WinRT API and must check `CheckAvailabilityAsync` first to confirm the device supports it before offering Face ID/Touch ID as options — don't show them as available if the hardware doesn't support them.

---

## 4. Build & deliverables

- Cross-compile via MinGW-w64, matching the existing GridNote/GameRadar/Scratchpad workflow. Provide a build script (`build.bat` or CMake) that produces `VaultService.exe` and `VaultUI.exe`.
- Provide install/uninstall steps for registering the service (`sc create`/`sc delete` or in-code `CreateService`/`DeleteService`).
- Every module above should compile and actually do what it claims — if WebView2 SDK headers/runtime aren't available in the build environment, say so explicitly and provide the integration code anyway with clear setup instructions, rather than silently omitting it.
- Port the existing liquid-glass HTML/CSS/JS UI (provided as `vault_ui_v2.html`) into `VaultUI/ui/` with minimal changes — only adapt the data layer (replace the hardcoded `apps` array and stats with real calls through `NativeBridge`/IPC to `VaultService`), keep all visual design, animations, and the icon set as-is.

---

## 5. What "done" looks like

- Launch `VaultUI.exe` → it shows real detected games/apps with real extracted icons (not 6 hardcoded sample entries).
- Lock a real installed app (e.g. Notepad or a small game) → launch it → it visibly does not open, auth overlay appears.
- Enter the correct PIN (set during onboarding) → app resumes and opens normally.
- Activity drawer shows that real session: actual login time, actual duration, actual logout time once you close the app, with failed-attempt count if you entered a wrong PIN first.
- Excluded processes (`node.exe`, `python.exe`, etc.) never appear in the detectable list even if running.

If any piece of this can't be fully implemented in the current environment (e.g. no Windows machine available to test WebView2/Hello against real hardware), implement the code as if it will run on a real Windows box, and clearly flag in your response which parts are untested-but-complete versus genuinely incomplete. Do not present untested code as verified, and do not fill gaps with mock logic.
