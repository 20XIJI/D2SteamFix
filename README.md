# D2SteamFix

[简体中文](README.zh-CN.md)

D2SteamFix is a launch wrapper for Destiny 2 intended to help alleviate frame pacing and stuttering issues for Steam users. Before starting Destiny 2, it temporarily denies execute access to Steam's overlay renderer DLLs, then restores the original permissions after exiting the game.

Please note that blocking the Steam Overlay DLLs can also affect overlay-dependent features such as Steam Input, notifications, Game Recording, and Remote Play. This program temporarily changes only the ACL metadata of the overlay DLLs; it does not alter their contents. It does not inject code, inspect process memory, modify game files, change Steam configuration, or bypass BattlEye.

## Installation

Download [steamfix.exe](https://pkg.d2checkpoint.com/D2SteamFix/steamfix.exe) and place it directly in your game's directory.

Then, open Destiny 2's launch options in Steam:

1. In Steam right-click **Destiny 2** in your library and select **Properties**.
2. Find the **Launch Options** field under the **General** tab and paste the command below.

```text
"C:\path\to\Destiny 2\steamfix.exe" %command%
```

Replace `C:\path\to\Destiny 2` with the actual location of your Destiny 2 installation. The example path below is for a default Steam installation; if your game is on another drive, use that drive letter and the complete path instead:

```text
"C:\Program Files (x86)\Steam\steamapps\common\Destiny 2\steamfix.exe" %command%
```

![Steam Launch Options](https://static.d2checkpoint.com/steamfix_launch_options.png)

## Verifying it works

Launch Destiny 2 from Steam and press **Shift+Tab** in game. The Steam overlay not appearing means the renderer is blocked and D2SteamFix is doing its job. The overlay returns on its own once the game exits.

D2SteamFix writes `steamfix.log` next to the game executable, in the same folder as `steamfix.exe`. That file is what to attach when reporting a problem; the error dialog also shows an error code that `docs/EXIT_CODES.md` explains.

## If the Steam overlay stays disabled

The renderer permissions are restored when the game exits. If Destiny 2, Steam or the computer stops abruptly, that restore never runs and the overlay stays disabled until D2SteamFix gets another chance.

Double-click `steamfix.exe` once to fix it. Run without a game to launch, it restores the saved permissions, removes its leftover state and then tells you it belongs in Steam's launch options — that message is expected, and the repair has already happened by the time it appears. Close Destiny 2 first: D2SteamFix waits for every Destiny process to exit before touching permissions.

## Removing D2SteamFix

Repair first, then remove — in that order:

1. Double-click `steamfix.exe` once, as described above, so no saved permission state is left behind.
2. Remove the launch options from Destiny 2's properties in Steam.
3. Delete `steamfix.exe`, `steamfix.log` and `steamfix.log.old` from the game folder.

Deleting `steamfix.exe` while a `steamfix.dat` file is still beside it leaves the Steam overlay permanently disabled, with nothing left installed to explain why. If that has already happened, put `steamfix.exe` back in the game folder, double-click it once, and then remove both files.

## Administrator rights

D2SteamFix refuses to run with an elevated token, because it would then launch Destiny 2 elevated as well. It never requests elevation itself — its manifest declares `asInvoker` — so when it reports one, the token was inherited from whatever started it.

The usual cause is Steam carrying a compatibility flag. Right-click `steam.exe`, select **Properties** → **Compatibility**, clear **Run this program as an administrator** (also under **Change settings for all users**), then restart Steam. The failure message names the process the token came from.

Systems with User Account Control switched off entirely (`EnableLUA=0`), and the built-in Administrator account, are a different case: every process already runs with a full administrator token and there is nothing to drop. D2SteamFix continues in that situation and records a warning in `steamfix.log`.

## Building from Source

### Prerequisites

Building D2SteamFix requires Windows and the Microsoft Visual C++ toolchain.

Install either Visual Studio or Visual Studio Build Tools with the Desktop development with C++ workload selected. Make sure to include the MSVC compiler and a Windows SDK. Run the following commands from a Visual Studio Developer command prompt:

```batch
make.cmd configure
```

```batch
make.cmd build
```

## Windows Defender

The published binary may be flagged as a false positive by Windows Defender. This is a known issue and Microsoft has confirmed that it was a false positive and has removed it.

If your Windows Defender still reports the old detection, [please follow these instructions to clear cached detections and update malware definitions.](docs/WINDOWS_DEFENDER.md)
