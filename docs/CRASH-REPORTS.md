# Logs and crash reports (0.1.4+)

## Where
| Build | Folder |
|---|---|
| Windows installer | `%LOCALAPPDATA%\MagicalBlocks\logs\` |
| Windows portable zip (`portable.txt` next to the exe) | `<game folder>\logs\` |
| Linux | `~/.config/MagicalBlocks/logs/` |

* `latest.log`: everything the game printed (stdout/stderr, RT64 renderer messages, including
  D3D12/Vulkan errors, which RT64 reports through stderr without needing the debug layer).
  The header lists version, OS (and whether it's wine), working directory, data folder, exe path
  and command line. Previous sessions rotate to `previous-1.log` … `previous-4.log`.
* `crash-<date>-<time>.txt`: written when the game dies. It has the kind (unhandled
  exception, abort, std::terminate, CRT invalid parameter, pure virtual call, stack overflow, or
  an unexpected `exit()` from inside the recompiled game/librecomp), the exception code, the faulting
  address as module+offset, registers, a stack trace (DbgHelp StackWalk64) and the module list.
  The report is also appended to `latest.log`.
* `crash-<date>-<time>.dmp` (Windows): minidump for WinDbg/Visual Studio.
* After writing the report, a message box tells the user where it is.

The newest 20 crash files are kept.

## Symbolizing
Release builds don't ship symbols. The mingw build writes a linker map
(`build-win64/MagicalBlocks.map`, and `tools/package-release.sh` keeps a copy in
`../dist/symbols/MagicalBlocks-<version>.map`, which is **local only**, never uploaded
with the release). Then:

    tools/symbolize_crash.py crash-20261018-102644.txt ../dist/symbols/MagicalBlocks-0.1.4.map

Each `MagicalBlocks.exe+0x…` frame gets its function name appended.

## Testing the reporter
`MTC_TEST_CRASH=segv|abort|throw` makes the game crash on purpose right after the log starts.

## Graphics API
Settings → Graphics → Graphics API (Auto / Vulkan / D3D12), or `--graphics-api auto|vulkan|d3d12`
on the command line (the flag overrides the setting for that run). Since 0.1.4 **Auto tries Vulkan first on
Windows and falls back to D3D12**. 0.1.3 used D3D12 by default and crashed at "Start game" on at
least one RTX 3070 Ti / Windows 11 machine. The API actually in use is shown under the option and
logged as `[renderer] graphics API: …`.
