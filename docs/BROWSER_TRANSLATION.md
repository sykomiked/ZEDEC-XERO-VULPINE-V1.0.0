# Browser translation matrix — the OS in WebAssembly

How every aspect of ZXV reaches the browser, what translates it, and what
remains. The rule: the browser never speaks a protocol the door does not
serve and the wasm does not parse; the translation layers are the door's
binary endpoints, the boot's adapters, and the wasm's compiled surfaces.

## The translation layers

1. **The door (kernel, port 80)** — the binary endpoints the browser
   calls: `/cisex` (168-byte UBH envelopes), `/bank` (digest →
   bank/len/offset), `/telemetry`, `/spaces`, `/card` (sovereign-key
   seat), `/pk` + `/sig` (the ML-DSA reply signatures), `/l2` (the state
   bus stream). This is the OS-to-web wire: raw bytes, no JSON bloat.

2. **boot.js** — the adapter: fetches the endpoints, decodes the envelopes
   into the reactive state (the catalogue, the search results, the spaces,
   the seat), and drives the wasm's exports through a tested ABI
   (pointer-plus-length, never raw strings).

3. **The wasm** — the compiled surfaces: the boot/desk shell (zui), the
   login (zui_login), the launch theme. What the wasm does NOT yet carry:
   the app runtime (zxapp + the pillars) — that is the porting program
   below.

## The matrix

| OS aspect | Browser translation today | Gap |
|---|---|---|
| The desk, login, launch | The wasm (zui) — live | — |
| The catalogue (407 apps) | boot.json + the pack, rendered by the wasm | The apps themselves (the 378 pending) |
| The content index | `/cisex` envelopes → the search cards | — |
| The shared spaces | `/spaces` → the wasm's Spaces face | The chat/channel surface (the IRC adapter) |
| The seed bank | `/bank` metadata + the pack slices | The browser-side bank client (next) |
| The sovereign key | WebCrypto Ed25519 + `/card` | The custody model (audit 16) |
| The reply signatures | `/pk` + `/sig` — the client verifies | The boot's verifier adapter (next) |
| The telemetry | `/telemetry` key=value lines | The dashboard rendering |
| The VNA mesh | Not in the browser | WebTransport/WebRTC (future) |
| The app runtime | Not in the wasm | The pillar ports (below) |
| 36n9 URIs | The resolver CLI + the gateway fallback | The wasm's native URI handler |
| 9N63 URIs | The telemetry/spaces endpoints | The wasm's state-plane surface |

## The next translation layers, in order

1. **The browser bank client**: fetch the bank file once, slice seeds
   locally (the pack's slicing already works — the same reader over the
   bank). `36n9://bank.<id>/<offset>` resolves in the page with no round
   trip.
2. **The signature verifier**: the boot hashes every door reply and
   checks `/sig` against the node's `/pk` — the audit's reply-integrity
   chain, client-side.
3. **The 36n9/9N63 native surface**: the wasm's address bar parses
   `36n9://` URIs (the kernel's parser logic mirrored), renders bank
   slices, and the state plane's telemetry/spaces as native views.
4. **The app runtime in the wasm**: port zxapp + the four pillars
   (eve/kron/myra/sutra) to wasm32. The pillars are hosted-only today;
   the port is the big program — the apps then run in the browser with
   the same K2 checks the nodes run.

## The audit's browser findings, folded in

- The ABI is pointer-plus-length with bounds checks in C (finding 4).
- The auth fails closed (finding 3 — fixed).
- The capability wording is honest: the boot shows what it runs (finding
  10), and the release publishes source and artifacts from one commit
  (finding 8).