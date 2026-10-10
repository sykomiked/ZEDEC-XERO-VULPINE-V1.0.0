<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZXV for Mac

ZXV.app runs the ZXV swarm on your Mac as an ordinary app. It sits next to macOS and never replaces or reconfigures it. One download runs on both Apple Silicon and Intel Macs (macOS 10.15 Catalina or later).

This page covers installing, first run, where data lives, how the app behaves offline, how a release manager signs and notarises it, and what it cannot do yet.

## Install

1. Open `ZXV-<version>-macos.dmg`.
2. Drag **ZXV** onto **Applications**.
3. Open ZXV from Applications or Launchpad.

A notarised release opens straight away. A build that is not notarised, such as a CI build from a pull request or a local build without signing variables, makes Gatekeeper warn the first time. Right-click ZXV, choose **Open**, then choose **Open** again.

To uninstall, quit ZXV, then delete the app and the folders listed in [Where data lives](#where-data-lives).

## First run

1. ZXV opens a window with a Dock icon and a menu bar.
2. It starts its engine (`ZXV.app/Contents/MacOS/zxv-engine`) in the background. The engine scans the hardware, sizes the swarm and serves the window.
3. If no model is installed, ZXV asks once what to do:
   - **Fetch from IPFS.** Shown only when the app's model manifest lists a model. ZXV downloads it, checks it, installs it and restarts the swarm with it. You get a notification when it finishes.
   - **Choose GGUF File…** Pick a `.gguf` file you already have. ZXV reads it where it is and does not copy it.
   - **Later.** The swarm runs without a model. It cannot write answers until you install one.

Nothing is downloaded unless you say so, and ZXV never asks for notification permission at launch. macOS asks the first time ZXV has something to tell you.

The same choices stay available from the **Model** menu: **Choose Model File…**, **Fetch Model from IPFS…**, **Use No Model**, **Show Models Folder** and **Show Logs Folder**.

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
| Window data (cookies, storage) | Nowhere. The window uses a non-persistent WebKit data store. |

ZXV writes nothing else: no login item, no launch agent, no files outside these folders, and no system settings.

## How it fits on your Mac

- **It runs as a guest.** `swarm_governor` limits the swarm to at most 13/21 of the compute that is free, and always leaves at least 1/8 of RAM untouched. The swarm shrinks when you get busy.
- **It runs only while it is open.** Closing the window quits ZXV. The engine is tied to the app through a pipe, so it stops even if the app crashes or is force-quit.
- **It stays off the network.** The window is served on `127.0.0.1` with a port picked at launch, and never on a network interface. Every API request must carry a random 256-bit token that is new at every launch. The engine also rejects requests whose `Host`, `Origin` or `Sec-Fetch-Site` header shows they came from another site, so a web page in your browser cannot drive or read ZXV. This is the fix for gap 14; see `kernel/arch/hosted/zxv_http_guard.h`.
- **Links leave the app.** Links in the window open in your default browser. Only the engine's own page can load in the ZXV window.

## Offline, LAN and online

| Situation | What works |
|---|---|
| **Offline**, model installed | Everything the app does today. The engine, window, swarm and model slot need no network. |
| **Offline**, no model | The swarm runs. **Choose GGUF File…** works with any file on disk or a USB drive. |
| **LAN** | **Fetch Model from IPFS** tries a local IPFS node first (`http://127.0.0.1:8080`). To use a node elsewhere on your LAN, set `ZXV_IPFS_GATEWAYS`, for example `launchctl setenv ZXV_IPFS_GATEWAYS "http://nas.local:8080"`, then reopen ZXV. |
| **Online** | The fetch falls back to the public gateways `ipfs.io` and `dweb.link`. |

Wherever a model comes from, the fetcher installs it only if its exact size and SHA-256 match `models.manifest`. The manifest sits inside the signed app, so a hostile gateway or LAN peer can waste your time but cannot install a file. The CID locates the file; the pinned hash verifies it.

**Peer network (Vinea).** Off by default: the engine opens no socket beyond its 127.0.0.1 window and sends nothing until you choose **LAN** or **Online** under Network in the window (or start it with `--net lan|online`). LAN accepts and contacts private addresses only (10/8, 172.16/12, 192.168/16, 169.254/16, 127/8); Online any IPv4 address. UDP port 8723 by default (`--net-port`). Peers are added by address (`--peer IP:PORT`); there is no automatic LAN discovery yet. Every datagram is checked (ML-DSA-65 signature, replay window) before it is used. The node only routes (ping, find-node); it shares nothing.

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
  MacOS/ZXV                  native shell, macos/ZXVApp.m (AppKit + WKWebView), arm64 + x86_64
  MacOS/zxv-engine           engine, kernel/arch/hosted + every swarm and tensor source, arm64 + x86_64
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

### Signing and notarisation

`build_system/build_signed_app.sh --mac-app ZXV.app` and `--mac-dmg file.dmg` do the Apple signing; `build_desktop.sh` calls both. Each stage depends on environment variables and is skipped cleanly when they are absent:

| Variable | Meaning | If absent |
|---|---|---|
| `ZXV_SIGN_IDENTITY` | `Developer ID Application: <name> (<TEAMID>)` or its SHA-1 hash | The app is ad-hoc signed and nothing is notarised. |
| `ZXV_TEAM_ID` | Optional. The identity must belong to this team. | The team is not checked. |
| `ZXV_KEYCHAIN` | Optional keychain file that holds the identity | The default keychain search list is used. |
| `ZXV_NOTARY_PROFILE` | A `notarytool` keychain profile | Signed, not notarised. |
| `ZXV_NOTARY_KEY`, `ZXV_NOTARY_KEY_ID`, `ZXV_NOTARY_ISSUER` | An App Store Connect API key, the alternative for CI | Signed, not notarised. |

Release steps on a Mac:

1. Do once: put the Developer ID Application certificate in your login keychain, then store notary credentials:

   ```sh
   xcrun notarytool store-credentials zxv-notary --apple-id <id> --team-id <TEAMID>
   ```

2. Build, sign, notarise and staple:

   ```sh
   export ZXV_SIGN_IDENTITY="Developer ID Application: 36N9 Genetics, LLC (<TEAMID>)"
   export ZXV_TEAM_ID=<TEAMID>
   export ZXV_NOTARY_PROFILE=zxv-notary
   build_system/build_desktop.sh --mac-only
   ```

   This step:
   - Signs `zxv-engine`, then `ZXV.app`, from the inside out with no `--deep`. It uses the Hardened Runtime (`--options runtime`), a secure timestamp and `macos/ZXV.entitlements`.
   - Verifies the signature with `codesign --verify --strict`.
   - Zips the app with `ditto`, submits it with `notarytool submit --wait` and requires the status `Accepted`; on failure it prints the notary log.
   - Staples the app, then checks it with `stapler validate` and `spctl --assess`.
   - Builds the `.dmg` from the stapled app, then signs, notarises and staples the `.dmg`.

3. Check the result on a clean Mac: download the `.dmg` (so it is quarantined), open it, drag ZXV to Applications and open it. There should be no Gatekeeper warning.

**The entitlements file is empty on purpose.** Under the Hardened Runtime without the App Sandbox, ZXV needs no exceptions:

- The engine listens on loopback.
- The fetcher uses `curl`.
- WKWebView compiles JavaScript in WebKit's own processes.
- Nothing loads unsigned libraries.

Any entitlement you add weakens the runtime, so add one only for a stated reason.

**In CI**, the `macos-app` job in `.github/workflows/ci.yml` uses these repository secrets:

- `ZXV_SIGN_IDENTITY`, `ZXV_TEAM_ID`.
- `ZXV_CERT_P12_BASE64` and `ZXV_CERT_PASSWORD`: the certificate, imported into a temporary keychain that is deleted after the job.
- `ZXV_NOTARY_KEY_BASE64`, `ZXV_NOTARY_KEY_ID` and `ZXV_NOTARY_ISSUER`.

Without them, for example on forks and pull requests, the job still builds, tests, ad-hoc signs, launches and quits the app, and uploads it.

Never commit a certificate, key or password. The signing modes of `build_signed_app.sh` never read or write `build_system/keys/`. That directory belongs to the separate ZSP package-signing job, which is the script's default mode with no arguments.

## Honest limits

- **Answers are greedy and slow, and real models are untested.** The forward pass (`kernel/src/tensor/zt_model.c`) is compiled in through `kernel/arch/hosted/zxv_zt_glue.c`. The glue loads the model once, wraps the message in the ChatML template when the vocabulary has `<|im_start|>` (Qwen2, Qwen3), and decodes greedily, up to 256 new tokens in a context of at most 2048. `test_zxv_zt_glue.c` checks the reply against the tensor engine's reference chain on a tiny random test model; no real model has been run through the app. It is one thread of scalar C (zt_model.h estimates 2.5 to 6 tokens a second for a 0.5B model; not measured), and `/api/ask` blocks the window while it runs. Only F32, F16, BF16, Q8_0, Q4_0, Q4_K and Q6_K weights load: Q4_K_M files of models whose width is not a multiple of 256 (Qwen2.5-0.5B, 896 wide) contain Q5_0 tensors and are refused, so use Q8_0 files. The model answers on its own: the swarm's market does not yet budget its tokens, and the swarm's agents are still stand-ins.
- **Tokenizers.** Only byte-level BPE tokenizers (Qwen2, Llama 3) are supported. SentencePiece models (Llama 2, Mistral, Gemma) load as metadata only.
- **Peer networking is basic.** The app runs one Vinea node over UDP (routing only, a new identity at each launch, IPv4, no NAT traversal or LAN discovery); `ipfs_node` file exchange, calls and social have no host glue yet.
- **Not sandboxed.** The engine is a separate process that reads a user-chosen model by path. Sandboxing would need `app-sandbox` plus `inherit` on the helper, network client and server entitlements, and security-scoped bookmarks for user-chosen files. It is required for the Mac App Store, not for Developer ID distribution.
- **No self-update.** There is no update check yet. A hybrid-signed update path is gap 8 and gap 13.
- **Not verified on a real Mac here.** The native shell (`macos/ZXVApp.m`), the Apple-clang universal build, icon rendering, signing, notarisation and stapling were written without a Mac. They are exercised only by the `macos-app` CI job on GitHub's `macos-14` runner. Intel coverage is the universal binary's x86_64 slice; nothing runs on an Intel Mac in CI.
- **Notifications** need the app to be signed. An ad-hoc-signed local build may not be allowed to post them.
- **The model fetch has no progress bar.** Watch `~/Library/Logs/ZXV/fetch.log`. A notification says when it finishes.
- **Windows and Linux** keep the engine-only build: the window opens in the default browser. They now have the same token and Host and Origin protection.
