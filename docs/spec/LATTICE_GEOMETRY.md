<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Lattice geometry

This page only points to where the lattice coordinate spec lives, so it is
written down once:

- **The coordinate and its binding to the layer hashes:** MATRIX.md
  section 8. A matrix word in grid domain 0x30 names a layer, a booklet or a
  chunk. One resolver (`kernel/src/alexandria/alx_lattice.h`) walks from the
  root CID to checked bytes for the runtime, a browser (WASM) and bare metal
  alike. `test_alx_lattice` checks that the block store and the disk image
  agree.
- **The grid word and its domains:** GRID_CHAIN.md.
- **What goes in each of the 145 layers, and booklet 000:** LAYER_MAP.md.
- **The node formats:** MATRIX.md section 5.
