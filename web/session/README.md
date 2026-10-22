<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# After the browser boot: login, spaces, companion

The boot screen (`web/boot`) hands over to this page. It runs on the person's own machine, served by their zxvd:

```sh
tools/web/build_web_root.sh /tmp/zxv-web --brand path/to/gold-svgs   # --brand is optional
zxvd --rpc "$HOME/.zxv/run/zxvd.sock" --web-root /tmp/zxv-web
# open the ZXV-WEB: line's link (keep the #token part private)
```

| step | what happens | where |
|---|---|---|
| login | The passport rebuilds the identity from a seed, or from a passphrase and handle, inside the page. Nothing is looked up and nothing is sent. The bar fills as each step really ends. A refused seed or passphrase is named in words. | `passport_worker.js`, `zxv_passport.wasm` (`zxv_passport_wasm.c` on `kernel/src/passport`) |
| first time | "New here" makes a seed from the browser's `crypto.getRandomValues`, shows it once to write down, then has it typed back. The module keeps it pending, and a seed that is not the one shown is refused. Only then is the identity built, exactly as a typed seed builds it. | `pw_seed_new`, `pw_seed_confirm` |
| spaces | `spaces.list` from zxvd | `session.js`, zxvd's `/api/rpc` |
| space | the space's apps; the companion opens in it | `session.js` |
| companion | `swarm.ask` runs the model zxvd has loaded, under the budget gate and the answer tag. The page says who wrote the answer from zxvd's own record (`swarm.state` `companion.answered_by_model` and the tag). When no model wrote it, the page says so. | `session.js` |

The page reaches zxvd only through the window's guarded `/api/rpc`. zxvd adds the launch token to every page it serves,
as it does for its own window. `zxv_passport.wasm` is checked against `boot.json` before it runs.

What the page does with what people type and with failures:

- Seed text is read as people paste it: full-width letters, other dashes and line breaks become plain separators.
  Anything else (control bytes, a NUL, look-alike letters) is refused in words, never skipped. Each typo position is
  caught by the seed's 16-bit check (a random typo slips past it about once in 65,536 tries; that is the seed format,
  shared with every other device).
- A passphrase is up to 256 bytes and at least 16, a handle 1 to 64 bytes, counted as UTF-8 bytes. Longer input is
  refused, never cut to fit. A passphrase is taken byte for byte, as typed: no trimming and no Unicode normalization,
  as the passport defines it.
- After a seed is confirmed or used, it is in no field, storage, URL or console line, and not in the page's JS heap
  (the page clears the engine's last-match slot, which would otherwise keep the text alive).
- A boot file or the passport wasm that is missing, cut short, not the manifest's hash, or stops arriving (15 s at the
  boot, 30 s in the page) stops in words. zxvd going away mid-ask is said in words, and the page asks again once zxvd
  is back on its port and token.

`make -C kernel test-passport-wasm` checks the glue against the passport, including every one-character mistake in a
seed. `tests/browser/test_browser.sh` drives boot, login, space and answer in headless Chromium, then
`test_browser_stress.cjs` the same path under abuse (bad input, broken boot, zxvd killed, corrupt models, tabs at once,
a 100-round soak, secrets looked for afterwards).

## Not there yet

- zxvd does not know who logged in. The identity stays in the page, and zxvd's session is the launch token. Linking the
  two needs zxvd passport methods.
- Capsule sync, café mode and spending. The page rebuilds the identity, and the rest of `kernel/src/passport` is not
  wired in.
- The desktop's window manager and the native zui login screen (`kernel/src/zui/zui_login.h`) are not in the browser.
  These views are HTML on the same palette.
- An answer is shown when it is complete. `swarm.ask` does not stream, so there is no progress bar for it.
