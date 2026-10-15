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
| spaces | `spaces.list` from zxvd | `session.js`, zxvd's `/api/rpc` |
| space | the space's apps; the companion opens in it | `session.js` |
| companion | `swarm.ask` runs the model zxvd has loaded, under the budget gate and the answer tag. The page says who wrote the answer from zxvd's own record (`swarm.state` `companion.answered_by_model` and the tag). When no model wrote it, the page says so. | `session.js` |

The page reaches zxvd only through the window's guarded `/api/rpc`. zxvd adds the launch token to every page it serves,
as it does for its own window. `zxv_passport.wasm` is checked against `boot.json` before it runs.

`make -C kernel test-passport-wasm` checks the glue against the passport. `tests/browser/test_browser.sh` drives boot,
login, space and answer in headless Chromium.

## Not there yet

- zxvd does not know who logged in. The identity stays in the page, and zxvd's session is the launch token. Linking the
  two needs zxvd passport methods.
- Capsule sync, café mode and spending. The page rebuilds the identity, and the rest of `kernel/src/passport` is not
  wired in.
- The desktop's window manager and the native zui login screen (`kernel/src/zui/zui_login.h`) are not in the browser.
  These views are HTML on the same palette.
- An answer is shown when it is complete. `swarm.ask` does not stream, so there is no progress bar for it.
