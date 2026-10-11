# ZXV Studio

The native Mac app for ZXV, in SwiftUI (macOS 13 and later, Intel and Apple
Silicon). It drives **zxvd** over its JSON-RPC control socket
([docs/ZXVD.md](../../docs/ZXVD.md), [api/zxvd.openrpc.json](../../api/zxvd.openrpc.json))
and never links the kernel itself. The design is the approved five-view mockup
(Local AI Studio, Sicily Ledger & Patron Deck, P2P Mesh, Storefront & Escrow,
Settings & Jurisdiction Bridge).

| Part | What it is |
|---|---|
| `Sources/ZXVRPC` | Foundation-only socket client: typed results, exact minor-unit amounts, `net.command` lines, starting zxvd. Builds and is tested on Linux too. |
| `Sources/ZXVStudio` | The SwiftUI app, a menu bar item, Dock badge and macOS notifications for zxvd's notes. Mac only. |
| `Tests/ZXVRPCTests` | Unit tests, and live tests against a real zxvd when `ZXVD_BIN` is set. |
| `build_studio_app.sh` | Builds `ZXV Studio.app` (universal) with zxvd and `libzxv.dylib` inside it, ad-hoc signed. |

## Build

```sh
kernel/arch/hosted/build_zxvd.sh /tmp/zxvd                       # any OS
(cd macos/studio && ZXVD_BIN=/tmp/zxvd/zxvd swift test)          # any OS
sh macos/studio/build_studio_app.sh dist                         # Mac, Xcode 15+
open "dist/ZXV Studio.app"
```

The app is ad-hoc signed only (no Developer ID yet), so the first launch
needs right-click, then Open.

## How it finds zxvd

1. `ZXVD_SOCKET`, else `~/Library/Application Support/ZXV/run/zxvd.sock` (the
   same socket the AppKit shell `macos/ZXVApp.m` gives zxvd).
2. If nothing answers there, it starts the zxvd inside its bundle with
   `--rpc <socket> --exit-with-parent`, holding its stdin, so that zxvd stops
   when the app quits. A zxvd started by something else keeps running.

`ZXVStudio --smoke [SOCKET]` connects the same way, reads every view's calls
once, prints a summary and exits; CI runs it on the built app.

## Panels waiting on zxvd

Each panel below shows a dashed "not served yet" box until zxvd lists the
method in `rpc.discover`. The names are proposals to the zxvd owner, not
final.

| Panel | Method |
|---|---|
| Player Card tier | `player.card` |
| Escrow holds | `ledger.holds` |
| Open sell offers and wants | `net.offers` |
| Per-peer list | `net.peers` |
| Jurisdiction Bridge (clearing mode, accounting standard) | `jurisdiction.get` |

zxvd also has no per-currency minor-unit or currency-list method, so the app
keeps the ISO 4217 exponents itself (`Money.swift`) and lists the currencies
its accounts use.
