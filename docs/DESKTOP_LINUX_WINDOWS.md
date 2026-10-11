<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# ZXV on Linux and Windows

The Linux and Windows apps are the same engine as the Mac app: **zxvd**, the
swarm with its JSON-RPC control socket, linked to libzxv through its frozen
C ABI ([ZXVD.md](ZXVD.md)). The window is the existing browser UI, served on
127.0.0.1 only with a token that changes at every launch. The native SwiftUI
shell is Mac only.

| | Linux (x86_64, arm64) | Windows (x64) |
|---|---|---|
| Packages | `.deb`, `.AppImage`, portable `.tar.gz` | `.zip` with `ZXV.exe` |
| Needs | glibc 2.35 or later (Ubuntu 22.04, Debian 12, Fedora 36 and newer) | Windows 10 version 1803 or later, 64-bit |
| Start | `zxv`, or ZXV Swarm in the applications menu | `ZXV.exe` |
| Control socket | `$XDG_RUNTIME_DIR/zxv/zxvd.sock`, else `~/.local/state/zxv/run/zxvd.sock` | `%LOCALAPPDATA%\ZXV\run\zxvd.sock` |
| Models | `~/.local/share/zxv/models` (`zxv fetch-model` downloads the default) | `%APPDATA%\ZXV\models` |
| Engine log | the launching terminal or the session journal | `%LOCALAPPDATA%\ZXV\logs\engine.log` |

Starting ZXV while it runs opens the running one's window instead of a second
engine. Quit from the window, or send `zxvd.shutdown` over the socket; on
Linux a logout or `kill` also stops it cleanly and removes the socket.

## Installing

- **.deb**: `sudo apt install ./ZXV-<version>-linux-<arch>.deb`. Files go to
  `/usr/lib/zxv`, the command to `/usr/bin/zxv`.
- **AppImage**: `chmod +x ZXV-*.AppImage` and run it. Without FUSE, run it
  with `APPIMAGE_EXTRACT_AND_RUN=1`.
- **.tar.gz**: unpack and run `./install.sh` (installs under `~/.local` for
  this user only), or run `usr/bin/zxv` in place.
- **Windows**: unzip and start `ZXV.exe`, keeping it with `zxvd.exe` and
  `libzxv.dll`. The app is not code-signed, so SmartScreen may warn that it
  is unrecognised: choose More info, then Run anyway.

## The control socket

The same JSON-RPC 2.0 methods on every platform, for the SDKs and scripts.
Only your user can connect:

- Linux: the socket file is mode 0600 in a folder created 0700, and each
  peer's uid is checked (`SO_PEERCRED`).
- Windows: AF_UNIX sockets in Winsock. The folder zxvd creates has a
  protected DACL for your user alone (an existing one must be yours, not a
  link, and writable only by you, SYSTEM and Administrators); the socket
  file gets an owner-only DACL; and each peer process's user SID must match
  (`SIO_AF_UNIX_GETPEERPID`).

```sh
# Linux
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"zxvd.version"}' \
  | nc -U "${XDG_RUNTIME_DIR:-$HOME/.local/state/zxv/run}"/zxvd.sock
```

## Building

`build_system/package_desktop.sh linux|windows [OUTDIR]`:

- `linux` builds for this machine's architecture with `CC` (default `cc`)
  and makes the `.deb` (with `dpkg-deb`) and the `.tar.gz`. The AppImage is
  made when `ZXV_APPIMAGE_RUNTIME` names the AppImage type-2 runtime; its
  SHA-256 must be one of those pinned in the script, so a new upstream
  runtime is checked before it ships. Needs `mksquashfs`.
- `windows` cross-builds with MinGW-w64 (`CC`, default
  `x86_64-w64-mingw32-gcc`), from Linux or MSYS2. libzxv becomes
  `libzxv.dll`; its locks map to Windows SRW locks (`sdk/libzxv/src/win/`),
  so it needs no MinGW runtime DLL.

CI (`.github/workflows/desktop.yml`) builds every package, installs the
`.deb` on Linux x86_64 and arm64 and drives the installed zxvd end to end
over its socket (`kernel/arch/hosted/test_zxvd.py`), runs the AppImage, and
on a Windows runner runs libzxv's C tests and the same end-to-end test
against the packaged `zxvd.exe`.

## Honest limits

- Wine has no AF_UNIX sockets, so the Windows socket is tested only on a
  real Windows runner.
- Linux packages are built on Ubuntu 22.04, so they need glibc 2.35 or
  later; older distributions need a source build.
- No Windows ARM64 build yet, and no installer (MSI): the `.zip` is the
  install.
- The `.deb` and AppImage are unsigned; the release's SHA256SUMS file is the
  check, as for the Mac app.
