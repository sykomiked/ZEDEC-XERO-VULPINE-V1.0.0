<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The model in a browser tab

The app's integer engine (`kernel/src/tensor`) built to WebAssembly, with
WebGPU beside the CPU kernels. The same model and prompt give the same tokens
and the same logits digest as the app and the golden disc; that equality is
the test.

## Build

```
sh web/ai/build.sh OUT        # needs emcc (emsdk, emdawnwebgpu port)
```

OUT then holds `zxv_ai.js`, `zxv_ai_worker.js`, `zxv_ai_engine.js` and
`zxv_ai_engine.wasm`; serve them together.

## Use

```js
import { openAI } from './ai/zxv_ai.js';
const ai = await openAI({
    model : entry,          // boot.json's models entry: { name, cid, bytes, sha256, path, quant }
    device : 'auto',        // 'auto' | 'webgpu' | 'cpu'
    onProgress : (bytes, total, phase) => {},   // 'fetch' | 'check' | 'load'
});
ai.device, ai.record        // what runs the model; the determiner's line
const r = await ai.generate(prompt, { maxTokens, onToken : (text, id) => {}, signal });
// r: { text, ids, nPrompt, nGen, digest, stopped: 'eos' | 'max' | 'ctx' | 'abort' }
ai.close();
```

- The model is read from `entry.path` (relative to `entry.base`, default the
  page), then from any `entry.urls`. It must be exactly `bytes` long and
  match `sha256` before anything runs; a raw CID (`bafkrei...`) must name the
  same digest. A chunked UnixFS CID is held to its `sha256`.
- `auto` measures WebGPU and the CPU once per model and GPU and keeps the
  choice in `localStorage`; a later visit reuses it only after a run gives
  the logits it was measured with. `webgpu` puts every pass on the GPU;
  `cpu` never touches it.
- One request at a time; later ones queue. An abort stops a reply between
  tokens. Text arrives cut at whole UTF-8 characters.
- The model runs in its own Worker, so the page never waits on it.

## Test

`make -C kernel test-ai-web` (part of `verify-all`): the engine built
natively answers as the app does and matches `test_tiny.zw`; with emcc the
wasm build runs in Node (CPU) and Deno (WebGPU) and must print the native
lines exactly; Deno then runs what a page does (`test_zxv_ai_page.mjs`).
`ZXV_AI_MODEL=<a .gguf>` adds a real model; `ZXV_AI_WEB_REQUIRE=1` turns
each SKIP into a failure.
