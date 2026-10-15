<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Booting ZXV in a browser

Booting in a browser is still booting: the owner calls it "the space between spaces". This page runs the same boot
as bare metal and the desktop app, with the same screen, the same sounds and the same vault. It contains no video and
no recording. `zui_boot` (kernel/src/zui/zui_boot.h), built to WebAssembly, draws every frame and synthesises every
sound as the system loads, and it keeps pace with that load.

| file | what it is |
|---|---|
| `index.html` | the page: a canvas on #002911 and a line for saying why, if the boot stops |
| `boot.js` | streams the sealed files, feeds the real byte count to the boot screen, puts frames on the canvas and sound on the speakers |
| `zxv_boot.wasm` | `zui_boot` plus the vector renderer, the launch screen and the boot sounds (`tools/web/build_boot_wasm.sh`; `--check` proves it matches its source) |
| `zxv_boot_wasm.c` | the wasm's exports |

## Building the wasm

`zxv_boot.wasm` is sealed with its own CID, so it has to rebuild byte for byte. Each clang release emits different
code from the same source, so the committed file is built with one pinned release, clang 23.1.0 (the `PIN` line in
`tools/web/build_boot_wasm.sh`). Writing or checking the committed file refuses any other release; set `CLANG` to the
pinned compiler if it is not first on PATH. `--out FILE` builds a copy with any clang for trying the page out, and that
copy will not match the sealed file. Moving to a newer clang means changing the pin and the committed `.wasm` in one
commit, which gives the boot screen a new CID.

## What a sealed root needs

```
index.html  boot.js  zxv_boot.wasm          (from web/boot/)
brand/36n9-gold.zxvg  brand/acoto-gold.zxvg  brand/ascw-gold.zxvg  brand/smic-gold.zxvg  brand/cis-gold.zxvg
boot.json                                    (tools/web/make_boot_manifest.py ROOT [--next PAGE] FILE...)
...the system's own files, listed in boot.json
```

The logos are the gold brand vectors converted with `tools/zxvg/svg2zxvg.py`. For the crest, use the 3-level trace
`ascw-small.svg`. The full seal's stacked layers exceed the renderer's per-path limit (`ZUI_VEC_MAX_PTS`), so only its
dark silhouette would draw. `boot.json` lists every file with its size and SHA-256. The page counts each byte as it
arrives and checks each file against its hash before calling the boot ready. A file that is missing or does not match
stops the boot where it stands. The gong does not sound, and the page says why. The fox is built into the wasm.

## The sequence

1. The chime sounds as the screen first shows.
2. The bar under the tiles gathers with the bytes. The tiles light in pairs from the outside in, and each tile rings a
   coin as its gold comes up. The crest lights last.
3. Once every byte is in, the fox lights and the bar under it breathes while the files are checked.
4. Ready: the gong sounds, the bolts throw back, and the doors slide open. The page fires `zxv:booted` and goes to
   `next` if `boot.json` names one. With reduced motion, the doors open at once.

Browsers let a page play sound only after the person touches it. Until then the sound is still made and dropped at
the same pace, so any cue the person hears is the right one for that moment.

## Not there yet

- What the page hands over to. No browser build of the desktop, the login screen or the kernel exists yet. The doors
  open onto the ground, and `next` is where that build goes once it exists.
- CID fetches. Files are fetched by path and checked by SHA-256. No browser CID resolver exists, so content is not yet
  fetched by CID from peers or gateways.
- The intro videos and app launch screens in the browser. The intro is this boot screen, rendered live. App launch
  screens need the desktop's window manager in the browser, which arrives with the browser desktop build.
- The full ASCW seal as a vector. It needs a higher `ZUI_VEC_MAX_PTS` or a re-trace with fewer points per path.
- An AudioWorklet player. Sound is scheduled in 43 ms blocks from the page's frame loop, so a stalled tab can leave a
  short gap.
