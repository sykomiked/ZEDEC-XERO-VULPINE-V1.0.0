<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZXV for Mac

ZXV.app runs the ZXV swarm on your Mac as an ordinary app. It sits next to macOS and never replaces or reconfigures it. One download runs on both Apple Silicon and Intel Macs (macOS 10.15 Catalina or later).

This page covers installing, first run, where data lives, how the app behaves offline, how a release is signed, and what it cannot do yet.

## Install

1. Open `ZXV-<version>-macos.dmg`.
2. Drag **ZXV** onto **Applications**.
3. Open ZXV from Applications or Launchpad.

ZXV releases are not signed with an Apple Developer ID and are not notarised; the project has no Apple developer account and does not need one. The app is ad-hoc signed, which Apple Silicon requires for any code to run. Gatekeeper therefore blocks the first open of a downloaded copy once. Allow it one of these ways:

- **macOS 15 Sequoia and later.** Open ZXV and choose **Done** in the warning. Open **System Settings > Privacy & Security**, scroll to **Security**, choose **Open Anyway** next to the line about ZXV, and confirm with your password. Then open ZXV again and choose **Open Anyway**.
- **macOS 14 Sonoma and earlier.** Control-click (or right-click) ZXV in Applications, choose **Open**, then choose **Open** again in the warning. The **Privacy & Security** route above also works.
- **Terminal.** `xattr -dr com.apple.quarantine /Applications/ZXV.app` removes the download flag, after which ZXV opens normally. Do this only for a copy you downloaded from the project's own release page and whose checksum matches the release notes.

macOS remembers the choice, so later opens and updates of the same copy go straight through.

To uninstall, quit ZXV, then delete the app and the folders listed in [Where data lives](#where-data-lives).

## First run

1. ZXV opens a window with a Dock icon and a menu bar.
2. It starts its engine (`ZXV.app/Contents/MacOS/zxv-engine`) in the background. The engine scans the hardware and sizes the swarm. The window is the ZXV desktop itself, drawn natively (zui on Metal): a launcher rail with Burrow (files), Quillon, Glint, Spindle and Murmuration, each opening in its own window, and the Desktop / Spaces switch in the top bar. Its apps talk to the engine over its control socket; the bar says "zxvd connected" or "zxvd not running".
3. If no model is installed, ZXV asks once what to do:
   - **Download Model.** Shown only when the app's model manifest lists a model (this release lists Qwen2.5-1.5B-Instruct, Q4_K_M, about 1.1 GB, Apache-2.0). ZXV downloads it from IPFS when the manifest gives a CID, otherwise from the HTTPS mirror the manifest pins (the model's Hugging Face file at a fixed commit), checks its size and SHA-256, installs it and restarts the swarm with it. You get a notification when it finishes.
   - **Choose GGUF File…** Pick a `.gguf` file you already have. ZXV reads it where it is and does not copy it.
   - **Later.** The swarm runs without a model. It cannot write answers until you install one.

Nothing is downloaded unless you say so, and ZXV never asks for notification permission at launch. macOS asks the first time ZXV has something to tell you.

The same choices stay available from the **Model** menu: **Choose Model File…**, **Download Model…**, **Use No Model**, **Show Models Folder** and **Show Logs Folder**.

### Which model the engine uses

ZXV picks the first of these that exists:

1. The file you chose with **Choose Model File…**.
2. The first `*.gguf` file, by name, in `~/Library/Application Support/ZXV/models`. Files dropped in that folder by hand count too.
3. A model bundled inside the app (`ZXV.app/Contents/Resources/models`), when the release includes one.

The engine maps the file read-only and checks every header extent before it reads anything (`zt_gguf`). It then loads the model's byte-level BPE tokenizer (`zt_tok`); Qwen2 and Llama 3 tokenizers are supported. The window shows the model's name, architecture and status.

## Where data lives

| What | Where |
|---|---|
| Models you fetched or dropped in | `~/Library/Application Support/ZXV/models/` |
| Your model choice and the "asked once" flag | `~/Library/Preferences/com.36n9genetics.zxv-swarm.plist` (macOS user defaults) |
| Engine log | `~/Library/Logs/ZXV/engine.log`. It starts afresh once it passes 4 MB and never contains the access token. |
| Model download log | `~/Library/Logs/ZXV/fetch.log` |
| Window data | Nowhere. The window is drawn by the app itself; there is no browser and no web data store. |

ZXV writes nothing else: no login item, no launch agent, no files outside these folders, and no system settings.

## How it fits on your Mac

- **It runs as a guest.** `swarm_governor` limits the swarm to at most 13/21 of the compute that is free, and always leaves at least 1/8 of RAM untouched. The swarm shrinks when you get busy.
- **It runs only while it is open.** Closing the window quits ZXV. The engine is tied to the app through a pipe, so it stops even if the app crashes or is force-quit.
- **A control socket for you only.** Built on a Mac, the engine is zxvd and also listens on `~/Library/Application Support/ZXV/run/zxvd.sock`: a UNIX socket, mode 0600 in a 0700 folder, that answers only processes running as you (docs/ZXVD.md). The app uses it to stop the engine politely on Quit and to show unread notifications on the Dock icon; scripts and the SDKs can use it too. It is removed when the engine stops.
- **It stays off the network.** The window is served on `127.0.0.1` with a port picked at launch, and never on a network interface. Every API request must carry a random 256-bit token that is new at every launch. The engine also rejects requests whose `Host`, `Origin` or `Sec-Fetch-Site` header shows they came from another site, so a web page in your browser cannot drive or read ZXV. This is the fix for gap 14; see `kernel/arch/hosted/zxv_http_guard.h`.
- **No browser inside.** The Mac app's window loads no web page. The engine still serves its small web window on `127.0.0.1` for browsers (that is what Linux and Windows use), under the same token.

## Offline, LAN and online

| Situation | What works |
|---|---|
| **Offline**, model installed | Everything the app does today. The engine, window, swarm and model slot need no network. |
| **Offline**, no model | The swarm runs. **Choose GGUF File…** works with any file on disk or a USB drive. |
| **LAN** | For a model with a CID, **Download Model** tries a local IPFS node first (`http://127.0.0.1:8080`). To use a node elsewhere on your LAN, set `ZXV_IPFS_GATEWAYS`, for example `launchctl setenv ZXV_IPFS_GATEWAYS "http://nas.local:8080"`, then reopen ZXV. |
| **Online** | The fetch falls back to the public gateways `ipfs.io` and `dweb.link`. |

Wherever a model comes from, the fetcher installs it only if its exact size and SHA-256 match `models.manifest`. The manifest sits inside the signed app, so a hostile gateway or LAN peer can waste your time but cannot install a file. The CID locates the file; the pinned hash verifies it.

**Peer network (Vinea).** Off by default: the engine opens no socket beyond its 127.0.0.1 window and sends nothing until you choose **LAN** or **Online** under Network in the window (or start it with `--net lan|online`). LAN accepts and contacts private addresses only (10/8, 172.16/12, 192.168/16, 169.254/16, 127/8); Online any IPv4 address. UDP port 8723 by default (`--net-port`). Peers are added by address (`--peer IP:PORT`), or found on the local network: in LAN and Online modes the engine announces a signed node record on the multicast group 239.255.90.86, UDP port 8724, which never leaves the local link (`--no-lan-discovery` turns this off). The node keeps one identity (NodeID) across launches, with its peer cache, in `~/Library/Application Support/ZXV/net` (Linux: `~/.local/share/zxv/net`, Windows: `%APPDATA%\ZXV\net`; `--state-dir DIR` to move it, `--state-dir ""` for a fresh identity each launch). The identity file is readable by you only, like an SSH key, and is not encrypted at rest. A second copy of the engine on the same folder runs with a fresh identity. Peers report the address they see you at, and two peers behind different home routers can be introduced through a peer both know (UDP hole punching); Online mode keeps those paths open. Every datagram is checked (ML-DSA signature, replay window) before it is used. Nothing is shared until you share it.

**Updates.** Checked only when you press **Check for updates**, or once a day while the network is Online, against the gateway in `--update-gateway` (default `https://trustless-gateway.link`). Nothing is installed; a listing signed by no key you trust is shown as unsigned.

**Notifications.** Shown in the window. Update results and failed answers also go to Notification Center through `osascript` (it shows as Script Editor) or, on Linux, `notify-send`, when present; `--notify off` stops that.

## For the release manager

### What the build produces

`build_system/build_desktop.sh` builds, tests and packages the app:

```sh
build_system/build_desktop.sh              # every platform (Mac, Windows, Linux)
build_system/build_desktop.sh --mac-only   # just ZXV.app, the .dmg and the .app.zip
```

On a Mac with Xcode or the Command Line Tools, it produces this bundle:

```text
ZXV.app/Contents/
  Info.plist                 from macos/Info.plist.in (LSUIElement false: Dock icon and menus)
  MacOS/ZXV                  native shell, macos/ZXVApp.m (AppKit + the zui desktop on Metal), arm64 + x86_64
  MacOS/zxv-engine           engine: zxvd (docs/ZXVD.md), the swarm plus its JSON-RPC control socket, arm64 + x86_64
  Frameworks/libzxv.dylib    libzxv, the stable C ABI zxvd's books go through, arm64 + x86_64
  Resources/AppIcon.icns     from kernel/boot/zede_logo.png (needs Pillow)
  Resources/models.manifest  macos/models.manifest
  Resources/zxv-model-fetch.sh
  Resources/MAC_APP.md       this page (Help > ZXV Help)
  Resources/models/          a bundled model, if ZXV_BUNDLE_MODEL was set
```

Built on Linux, for example in `Dockerfile.desktop`, the native shell cannot be compiled. The bundle then holds only the engine, with `LSUIElement` set to true, and opens the window in the default browser. Release builds must come from a Mac: the `macos-app` CI job or a release manager's Mac.

Before it packages anything, the build runs these tests:

- `make -C kernel test-swarm`.
- `kernel/arch/hosted/test_hosted.sh`: the API-guard unit test under ASan and UBSan, then the end-to-end socket test, including the cross-site, DNS-rebinding and missing-token attacks, with and without the tokenizer test model installed.
- The same end-to-end test against the exact engine binary that ships.

To bundle a model, so that the first run needs no download, set `ZXV_BUNDLE_MODEL=/path/model.gguf`. Bundle only weights whose licence allows redistribution and commercial use (gap 17).

### Pinning models for the first-run fetch

Add one line per model to `macos/models.manifest`:

```text
NAME.gguf  SHA256  BYTES  CID  LICENCE
```

Take the CID from the release's boot manifest. Compute the hash and size from the exact file you pinned, with `shasum -a 256` and `wc -c`. The manifest ships with no entries, because no model has been pinned yet and an invented CID must never ship. Until a line is added, the app offers only **Choose GGUF File…**.

### Signing

`build_system/build_signed_app.sh --mac-app ZXV.app` and `--mac-dmg file.dmg` sign the bundle; `build_desktop.sh` calls both. Releases use the ad-hoc path, which needs no variables, certificate or Apple account:

- It signs `Frameworks/libzxv.dylib`, then `zxv-engine`, then `ZXV.app`, from the inside out with no `--deep`, using the Hardened Runtime (`--options runtime`) and `macos/ZXV.entitlements`.
- It verifies the result with `codesign --verify --strict`.
- Gatekeeper still asks once on first open; [Install](#install) lists the steps.

Notarisation is not part of the release plan. The script keeps an optional Developer ID path (`ZXV_SIGN_IDENTITY`, plus `ZXV_NOTARY_PROFILE` or the `ZXV_NOTARY_KEY` trio) for anyone who later builds with their own Apple account. With those variables unset, which is the normal case, it ad-hoc signs and skips notarisation with a log line.

**The entitlements file is empty on purpose.** Under the Hardened Runtime without the App Sandbox, ZXV needs no exceptions:

- The engine listens on loopback.
- The fetcher uses `curl`.
- The window is drawn on the CPU and shown through Metal; no code is compiled at run time.
- Nothing loads unsigned libraries.

Any entitlement you add weakens the runtime, so add one only for a stated reason.

**In CI**, the `macos-app` job in `.github/workflows/ci.yml` builds, tests, ad-hoc signs, launches and quits the app, and uploads it. It needs no secrets.

Never commit a certificate, key or password. The signing modes of `build_signed_app.sh` never read or write `build_system/keys/`. That directory belongs to the separate ZSP package-signing job, which is the script's default mode with no arguments.

## Honest limits

- **Answers are greedy and slow, and real models are untested.** The forward pass (`kernel/src/tensor/zt_model.c`) is compiled in through `kernel/arch/hosted/zxv_zt_glue.c`. The glue loads the model once, wraps the message in the ChatML template when the vocabulary has `<|im_start|>` (Qwen2, Qwen3), and decodes greedily, up to 256 new tokens in a context of at most 2048. `test_zxv_zt_glue.c` checks the reply against the tensor engine's reference chain on a tiny random test model; no real model has been run through the app. It is one thread of scalar C (zt_model.h estimates 2.5 to 6 tokens a second for a 0.5B model; not measured), and `/api/ask` blocks the window while it runs. F32, F16, BF16, Q8_0, Q4_0, Q5_0, Q5_1, Q4_K and Q6_K weights load, so Q4_K_M files of models whose width is not a multiple of 256 (Qwen2.5-0.5B, 896 wide, which contain Q5_0/Q5_1 tensors) are accepted; none has been run yet. The model answers on its own: the swarm's market does not yet budget its tokens, and the swarm's agents are still stand-ins.
- **Tokenizers.** Only byte-level BPE tokenizers (Qwen2, Llama 3) are supported. SentencePiece models (Llama 2, Mistral, Gemma) load as metadata only.
- **Peer networking is basic.** The app runs one Vinea node over UDP, IPv4 only (`kernel/arch/hosted/zxv_net_host.c`, linked into the engine through `kernel/arch/hosted/app_sources.sh`). It keeps its identity across launches, finds peers on the local link and can be introduced to peers behind other routers by UDP hole punching, as described under Peer network above; `ipfs_node` file exchange, calls and social have no host glue yet.
- **Not sandboxed.** The engine is a separate process that reads a user-chosen model by path. Sandboxing would need `app-sandbox` plus `inherit` on the helper, network client and server entitlements, and security-scoped bookmarks for user-chosen files. It is required for the Mac App Store, which ZXV does not use.
- **No self-update.** There is no update check yet. A hybrid-signed update path is gap 8 and gap 13.
- **Not verified on a real Mac here.** The native shell (`macos/ZXVApp.m`), the Apple-clang universal build, icon rendering, and ad-hoc signing were written without a Mac. They are exercised only by the `macos-app` CI job on GitHub's `macos-14` runner. Intel coverage is the universal binary's x86_64 slice; nothing runs on an Intel Mac in CI.
- **Notifications** may be refused for an ad-hoc-signed app on some macOS versions. The fetch log and the app window still show the result.
- **The model fetch has no progress bar.** Watch `~/Library/Logs/ZXV/fetch.log`. A notification says when it finishes.
- **Windows and Linux** keep the engine-only build: the window opens in the default browser. They now have the same token and Host and Origin protection.
