# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Windows + MSVC only (CMake hard-errors otherwise). Run from a Visual Studio Developer Command Prompt:

```batch
make.cmd configure   :: generate build/ (Visual Studio, x64)
make.cmd build       :: Release
make.cmd debug       :: Debug
make.cmd clean       :: remove build/ only (steamfix.log and steamfix.dat are preserved deliberately)
```

`/W4 /WX` — any warning fails the build.

There is no test suite and no lint config. Verification is manual: build, then run under Steam. When the working copy is on a non-Windows machine, neither build nor run can be performed — say so rather than claiming a change compiles.

## What the program is

`steamfix.exe` is a Steam launch wrapper for Destiny 2 (App ID 1085660). Steam invokes it as `steamfix.exe %command%`; it denies the *current user* execute access to Steam's `GameOverlayRenderer64.dll` (and `GameOverlayRenderer.dll` if present) via a DACL edit, launches the real `destiny2launcher.exe`, waits for the game to fully exit, restores the original DACLs, and returns the launcher's exit code.

It only edits ACL metadata. It does not touch DLL contents, inject, read process memory, or modify Steam config — keep it that way; anything resembling injection or memory access breaks the project's stated guarantees and its anti-cheat posture.

## Architecture

`src/main.cpp` (wWinMain) → `app::entry()` → `app::run()`. `app.cpp` is the whole orchestration; the other translation units are single-purpose helpers behind `include/steamfix/*.hpp`.

Sequence in `app::entry`/`app::run`:

1. Open `steamfix.log` in the CWD (Steam sets CWD to the game dir). Log failure is fatal — nothing else runs without it.
2. Global mutex `Global\D2SteamFix-1085660` enforces one instance; refuse to run elevated.
3. Recover any interrupted session (see below) before doing anything new.
4. Validate: first forwarded arg must resolve to a real `destiny2launcher.exe` with `destiny2.exe` beside it; `SteamAppId`/`SteamGameId` must be absent or `1085660`, at least one equal to it; no overlay DLL already loaded into this process (`overlay::loaded_renderer`); no Destiny process already running out of the same directory.
5. `steam::discover()` — parent-process chain first (walk ancestors looking for `steam.exe`), then `HKCU\Software\Valve\Steam` `SteamExe`, then `SteamPath`; each candidate verified by finding a regular `steam.exe`.
6. `acl::Protection::apply()` — back up DACLs, write the recovery journal, then apply deny ACEs.
7. Launch the launcher, `process::monitor()` until quiet, then `protection.restore()`.

Failures throw `AppError(ExitCode, stage, detail)` (`include/steamfix/error.hpp`), caught at the top of `entry()`, logged, shown in a message box, and returned as the process exit code. **`ExitCode` and `docs/EXIT_CODES.md` must be changed together.** `ExitCode::internal` (19) is the catch-all; nothing escapes `wWinMain`.

### ACL protection and crash recovery (`src/acl.cpp`)

The bulk of the code, and the part where mistakes leave a user's Steam install broken. The design is: never lose the original DACL, never restore over something that isn't ours.

- The deny ACE is `FILE_EXECUTE` only, `NO_INHERITANCE`, for the current user's SID, prepended ahead of the existing ACEs (`make_denied_acl`). It is applied through an already-open handle, never re-resolved by path.
- Every renderer handle is opened with `FILE_FLAG_OPEN_REPARSE_POINT` and validated: filename must be one of the two renderer names, the path absolute, the file regular and non-reparse, and a regular non-reparse `steam.exe` must sit beside it. The `(volume serial, file index)` identity is recorded and re-checked.
- After denying, `deny()` proves the result: ordinary read must still succeed and `GENERIC_EXECUTE` must fail with `ERROR_ACCESS_DENIED`.
- `restoration_state()` compares the file's live DACL against *both* the backed-up original and the expected denied form. Matching neither is an error that **preserves** the recovery state instead of overwriting — someone else changed the ACL, so hands off.
- `steamfix.dat` (CWD, `acl::recovery_path`) is the crash journal: DPAPI-encrypted (`CryptProtectData`), written to `.tmp` then `MoveFileEx`'d, owned by the current user with a protected two-ACE DACL, and containing the original descriptor plus file identity per target. Written **before** the first deny, removed **after** the last restore. `Protection`'s destructor is a best-effort second chance.
- On the next start, `recover_pending()` re-restores from it; if any entry can't be safely matched, nothing is restored and the journal survives for a later attempt. When the journal exists at startup, `app::entry` prompts the user and requires 30 seconds with no Destiny process (`process::wait_for_destiny_quiescence`) before touching anything.
- The framing has magic `STMFACL4` / `STMFDAT4` and hard size/count caps. Change the payload layout ⇒ bump the magic, or old journals will be misparsed. Every parse path is bounds-checked by hand (`require_range`, `checked_add`) and validated with `IsValidSid`/`IsValidAcl`/`IsValidSecurityDescriptor` — this parses attacker-influenced-ish bytes back into Win32 security structures, so keep it that way.

### Process monitoring is fail-closed (`src/process.cpp`)

`process::monitor` is `noexcept` and polls every 500 ms. Protection is released **only** on positive evidence of absence: both `destiny2launcher.exe` and `destiny2.exe` (matched by name *and* verified image directory) absent, observations reliable, for 2 consecutive polls — plus a 30 s grace period after launcher exit if `destiny2.exe` was never seen (the launcher can exit before the game appears). Any snapshot failure, un-openable candidate, or unexpected exception counts as *unreliable* and resets the release state, keeping the overlay blocked. Never "simplify" an error path here into an early release; a spurious restore mid-game is the failure mode this structure exists to prevent.

## Conventions

- Code comments are written in English, always. Chinese appears only in the user-facing message table (`src/i18n.cpp`), never in comments, identifiers, or log strings.
- Everything is `UNICODE`/wide internally; `narrow()`/`widen()` in `win32.hpp` convert only at the logging and message-box boundary. `display_path()` for paths in messages.
- RAII wrappers instead of raw cleanup: `UniqueHandle`, `LocalMemory` (`win32.hpp`), `RegistryKey` (`steam.cpp`).
- Path comparison goes through `paths::equal_windows` / `paths::name_is` (`CompareStringOrdinal` case-insensitive, `fs::equivalent` first) — never `==` on paths or `wcscmp` on filenames.
- Logging: `log.info/warn/error`, ASCII narrow strings, appended to `steamfix.log` with rotation at 4 MB. `Logger` methods are `noexcept` and swallow their own failures — logging must never be the thing that breaks a session.
- New `.cpp` files must be added to the `add_executable` list in `CMakeLists.txt`.
- Version lives in `project(... VERSION ...)`; it flows into `STEAMFIX_VERSION` (log lines) and `src/steamfix.rc.in`.
