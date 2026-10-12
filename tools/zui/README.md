# zui mockup renderer

`zui_mockup.c` draws the key ZXV screens with the native renderer in
`kernel/src/zui/` (the same code the shell will use), so review mockups show
real output rather than a design-tool picture. Sample data only.

Build and run (from the repo root):

```sh
gcc-11 -std=c11 -O2 -Wall -Werror -Wextra \
  -Ikernel/src/zui -Ikernel/src/font -Ikernel/src/emu \
  tools/zui/zui_mockup.c kernel/src/zui/zui_layout.c kernel/src/zui/zui_canvas.c \
  kernel/src/zui/zui_depth.c kernel/src/font/truetype.c kernel/src/emu/holo.c \
  -o /tmp/zui_mockup
/tmp/zui_mockup <font-dir> <out-dir>     # writes .ppm frames
/tmp/zui_mockup --tokens                 # design tokens as JSON for SwiftUI / Flutter
```

`<font-dir>` must hold IBM Plex Sans (Regular, Medium, SemiBold) and IBM Plex
Mono (Regular, Medium) as TTF. Plex is SIL OFL 1.1 and is not vendored here.

Frames: Studio at phone (390x844 pt @2x), laptop (1440x900) and large
(2560x1440); Writer with a held AI rewrite and with the Ask palette; the Studio
in amber/blue and red/cyan anaglyph, warm/cool tint, and a 12-frame
head-tracked parallax sweep.
